"""Janela principal: menus, projeto aberto e o servidor local dele.

Ciclo de um projeto: a pessoa escolhe a pasta (Home, menu ou linha de
comando) -> ``open_project`` valida e trava a pasta -> o servidor anterior cai
e sobe um novo apontando para ela -> quando ``/api/config/`` responde, o token
vira cookie do navegador embarcado e o editor carrega. Sem projeto, o mesmo
servidor sobe em modo Home e mostra os projetos recentes.
"""
import logging
import shutil
import sys
import urllib.request
from pathlib import Path

from PySide6.QtCore import QByteArray, QLockFile, QSettings, QTimer, QUrl, Qt
from PySide6.QtGui import QAction, QDesktopServices, QIcon, QKeySequence
from PySide6.QtNetwork import (
    QNetworkAccessManager, QNetworkCookie, QNetworkReply, QNetworkRequest,
)
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import (
    QApplication, QFileDialog, QMainWindow, QMessageBox, QProgressDialog,
)

from desktop import pages, paths
from desktop.browser import AppPage, SHELL_BASE, create_profile
from desktop.i18n import LANGUAGE_COOKIE, Translator, normalize, system_language
from desktop.server import TOKEN_COOKIE, TOKEN_HEADER, ServerProcess

sys.path.insert(0, str(paths.BACKEND_DIR))
from photoeditor.services import recent_projects  # noqa: E402  (sqlite3 puro, sem Django)

log = logging.getLogger(__name__)

# Textos do shell no idioma atual (o browser.py tambem usa).
tr = Translator(system_language())

HOME_PATH = '/home'
PROJECT_PATH = '/photos'
READY_POLL_MS = 250
HEALTH_POLL_MS = 2000
LOCK_NAME = '.photoeditor.lock'
ZOOM_STEPS = (0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0)


class MainWindow(QMainWindow):
    def __init__(self, devtools=False, frontend_url=None, port=None, debug=False):
        super().__init__()
        self.devtools = devtools
        self.frontend_url = QUrl(frontend_url) if frontend_url else None
        self.fixed_port = port
        self.debug = debug
        self.settings = QSettings(str(paths.SETTINGS_FILE), QSettings.Format.IniFormat)
        tr.language = normalize(self.settings.value('language')) or tr.language

        self.server = None
        self.project = None
        self.lock = None
        self.ready = False
        self.popups = set()
        self.devtools_window = None

        # Perfil pendurado na QApplication, nao na janela: tem de ser destruido
        # DEPOIS das paginas que o usam, senao o Qt avisa e pode travar ao sair.
        self.profile = create_profile(QApplication.instance(), devtools=devtools)
        self.profile.cookieStore().cookieAdded.connect(self._on_cookie)
        self.view = QWebEngineView(self)
        self.page = AppPage(self.profile, self.view, self)
        self.view.setPage(self.page)
        self.harden_view(self.view)
        self.setCentralWidget(self.view)

        self.net = QNetworkAccessManager(self)
        self.poll_timer = QTimer(self, interval=READY_POLL_MS, singleShot=True)
        self.poll_timer.timeout.connect(self._poll_ready)
        self.health_timer = QTimer(self, interval=HEALTH_POLL_MS)
        self.health_timer.timeout.connect(self._check_health)

        icon = paths.asset('favicon.png')
        if icon.is_file():
            self.setWindowIcon(QIcon(str(icon)))
        self.setMinimumSize(900, 600)
        self._build_menus()
        self._restore_geometry()
        self._update_title()

    # ------------------------------------------------------------------
    # Contrato com a AppPage (browser.py)
    # ------------------------------------------------------------------

    def app_origin(self) -> QUrl:
        if self.frontend_url:
            return self.frontend_url.adjusted(QUrl.UrlFormattingOption.RemovePath)
        return QUrl(self.server.origin) if self.server else QUrl()

    def harden_view(self, view):
        if not self.devtools:
            view.setContextMenuPolicy(Qt.ContextMenuPolicy.NoContextMenu)
        view.setZoomFactor(float(self.settings.value('zoom', 1.0)))

    def on_action(self, action, params):
        """``/__desktop__/<acao>`` do React e links das telas do shell."""
        log.info("Action: %s %s", action, params)
        path = params.get('path')
        if action in ('open-dialog', 'open') and not path:
            self.choose_project()
        elif action == 'open':
            self.open_project(path)
        elif action == 'new':
            self.new_project()
        elif action == 'home':
            self.go_home()
        elif action == 'reveal' and path:
            QDesktopServices.openUrl(QUrl.fromLocalFile(path))
        elif action == 'logs':
            self.open_logs()
        elif action == 'retry':
            self._start_server(self.project)
        else:
            log.warning("Unknown desktop action: %s", action)

    # ------------------------------------------------------------------
    # Projetos
    # ------------------------------------------------------------------

    def start(self, project=None):
        """Primeira tela: o projeto pedido na linha de comando, ou a Home."""
        if project:
            self.open_project(project)
        else:
            self.go_home()

    def go_home(self):
        self._release_project()
        self._start_server(None)

    def choose_project(self):
        start = str(Path(self.project).parent) if self.project else str(Path.home())
        folder = QFileDialog.getExistingDirectory(self, tr('dialog.open.title'), start)
        if folder:
            self.open_project(folder)

    def open_project(self, folder):
        path = Path(folder).expanduser()
        # Escolheu a propria raw/? O projeto e a pasta de cima.
        if path.name.lower() == 'raw' and not (path / 'raw').is_dir():
            path = path.parent
        if not path.is_dir():
            QMessageBox.warning(self, tr('project.missing.title'),
                                tr('project.missing.text', path=str(path)))
            return
        path = path.resolve()
        if self.project and path == self.project:
            return

        if not (path / 'raw').is_dir():
            box = QMessageBox(QMessageBox.Icon.Question, tr('project.noRaw.title'),
                              tr('project.noRaw.text', name=path.name), parent=self)
            create = box.addButton(tr('project.noRaw.create'), QMessageBox.ButtonRole.AcceptRole)
            other = box.addButton(tr('project.noRaw.other'), QMessageBox.ButtonRole.ActionRole)
            box.addButton(tr('common.cancel'), QMessageBox.ButtonRole.RejectRole)
            box.exec()
            if box.clickedButton() is other:
                self.choose_project()
                return
            if box.clickedButton() is not create:
                return
            (path / 'raw').mkdir()

        # Um projeto, uma janela: duas instancias no mesmo banco e na mesma
        # exportacao se atropelariam. QLockFile limpa trava de processo morto.
        (path / 'project_data').mkdir(exist_ok=True)
        lock = QLockFile(str(path / 'project_data' / LOCK_NAME))
        lock.setStaleLockTime(0)
        if not lock.tryLock(200):
            QMessageBox.warning(self, tr('project.locked.title'),
                                tr('project.locked.text', name=path.name, brand=paths.APP_NAME))
            return

        self._release_project()
        self.lock = lock
        self.project = path
        try:
            recent_projects.touch(paths.APP_DB, path)
        except Exception:
            log.exception("Could not record %s in the recent projects.", path)
        self._start_server(path)

    def new_project(self):
        folder = QFileDialog.getExistingDirectory(self, tr('dialog.new.title'), str(Path.home()))
        if not folder:
            return
        path = Path(folder)
        if not (path / 'raw').is_dir() and any(p for p in path.iterdir()
                                                if not p.name.startswith('.')):
            answer = QMessageBox.question(self, tr('project.nonEmpty.title'),
                                          tr('project.nonEmpty.text', name=path.name))
            if answer != QMessageBox.StandardButton.Yes:
                return
        raw = path / 'raw'
        raw.mkdir(exist_ok=True)
        files, _ = QFileDialog.getOpenFileNames(
            self, tr('dialog.import.title'), str(Path.home()), tr('dialog.import.filter'))
        if files:
            self._copy_photos(files, raw)
        self.open_project(path)

    def _copy_photos(self, files, raw):
        """Copia (nunca move) os JPEGs escolhidos para raw/."""
        progress = QProgressDialog(tr('dialog.import.progress'), tr('dialog.import.cancel'),
                                   0, len(files), self)
        progress.setWindowModality(Qt.WindowModality.WindowModal)
        progress.setMinimumDuration(300)
        failed = []
        for i, name in enumerate(files):
            if progress.wasCanceled():
                break
            source = Path(name)
            target = raw / source.name
            try:
                if not target.exists():
                    shutil.copy2(source, target)
            except OSError as exc:
                log.warning("Could not copy %s: %s", source, exc)
                failed.append(source.name)
            progress.setValue(i + 1)
            QApplication.processEvents()
        progress.close()
        if failed:
            QMessageBox.warning(self, paths.APP_NAME,
                                tr('project.importFailed') + '\n\n' + '\n'.join(failed[:20]))

    def reveal_project(self):
        if self.project:
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.project)))

    def open_logs(self):
        paths.LOG_DIR.mkdir(parents=True, exist_ok=True)
        QDesktopServices.openUrl(QUrl.fromLocalFile(str(paths.LOG_DIR)))

    def _release_project(self):
        if self.lock:
            self.lock.unlock()
        self.lock = None
        self.project = None

    # ------------------------------------------------------------------
    # Servidor local
    # ------------------------------------------------------------------

    def _start_server(self, project):
        self._stop_server()
        self._update_title()
        self._show_html(pages.loading(tr, project.name if project else None))
        self.server = ServerProcess(project, port=self.fixed_port, debug=self.debug)
        self.server.start()
        self.poll_timer.start()

    def _stop_server(self):
        self.poll_timer.stop()
        self.health_timer.stop()
        self.ready = False
        if self.server:
            self.server.stop()
        self.server = None

    def _poll_ready(self):
        server = self.server
        if not server:
            return
        if not server.alive():
            self._server_failed()
            return
        request = QNetworkRequest(QUrl(f'{server.origin}/api/config/'))
        request.setRawHeader(TOKEN_HEADER.encode(), server.token.encode())
        request.setTransferTimeout(1000)
        reply = self.net.get(request)
        reply.finished.connect(lambda: self._on_ready_reply(reply, server))

    def _on_ready_reply(self, reply, server):
        ok = reply.error() == QNetworkReply.NetworkError.NoError
        reply.deleteLater()
        if server is not self.server:
            return  # resposta de um servidor que ja foi trocado
        if not ok:
            self.poll_timer.start()
            return
        self.ready = True
        self.health_timer.start()

        cookie = QNetworkCookie(TOKEN_COOKIE.encode(), server.token.encode())
        cookie.setPath('/')
        cookie.setHttpOnly(True)
        cookie.setSameSitePolicy(QNetworkCookie.SameSite.Strict)
        self.profile.cookieStore().setCookie(cookie, QUrl(server.origin))

        base = self.frontend_url.toString().rstrip('/') if self.frontend_url else server.origin
        target = PROJECT_PATH if server.project else HOME_PATH
        # Um instante para o cookie chegar ao armazenamento antes da 1a requisicao.
        QTimer.singleShot(50, lambda: self.view.setUrl(QUrl(base + target)))

    def _check_health(self):
        if self.server and not self.server.alive():
            self._server_failed()

    def _server_failed(self):
        code = self.server.proc.returncode if self.server and self.server.proc else None
        log.error("Local server exited unexpectedly (code %s).", code)
        self.poll_timer.stop()
        self.health_timer.stop()
        self.ready = False
        self._show_html(pages.server_error(tr, paths.LOG_DIR / 'server.log',
                                           has_project=bool(self.project)))

    def _show_html(self, content):
        self.page.setHtml(content, SHELL_BASE)

    def export_running(self) -> bool:
        """A exportacao roda numa thread do servidor: fechar agora a interrompe."""
        if not (self.server and self.ready and self.project):
            return False
        request = urllib.request.Request(f'{self.server.origin}/api/export/',
                                         headers={TOKEN_HEADER: self.server.token})
        try:
            import json
            with urllib.request.urlopen(request, timeout=1) as response:
                return bool(json.load(response).get('running'))
        except Exception:
            return False

    # ------------------------------------------------------------------
    # Idioma
    # ------------------------------------------------------------------

    def _on_cookie(self, cookie):
        if bytes(cookie.name()).decode() != LANGUAGE_COOKIE:
            return
        lang = normalize(bytes(cookie.value()).decode())
        if lang and lang != tr.language:
            tr.language = lang
            self.settings.setValue('language', lang)
            self._retranslate()

    # ------------------------------------------------------------------
    # Menus
    # ------------------------------------------------------------------

    def _action(self, handler, shortcut=None, role=None):
        action = QAction(self)
        action.triggered.connect(handler)
        if shortcut is not None:
            action.setShortcut(QKeySequence(shortcut))
        if role is not None:
            action.setMenuRole(role)
        return action

    def _build_menus(self):
        Role = QAction.MenuRole
        Std = QKeySequence.StandardKey
        a = self.actions_by_key = {
            'menu.home': self._action(self.go_home, 'Ctrl+Shift+H'),
            'menu.open': self._action(self.choose_project, Std.Open),
            'menu.new': self._action(self.new_project, Std.New),
            'menu.reveal': self._action(self.reveal_project),
            'menu.close': self._action(self.go_home, Std.Close),
            'menu.quit': self._action(self.close, Std.Quit, Role.QuitRole),
            'menu.reload': self._action(lambda: self.view.reload(), 'Ctrl+R'),
            'menu.zoomIn': self._action(lambda: self._zoom(+1), Std.ZoomIn),
            'menu.zoomOut': self._action(lambda: self._zoom(-1), Std.ZoomOut),
            'menu.zoomReset': self._action(lambda: self._zoom(0), 'Ctrl+0'),
            'menu.fullscreen': self._action(self._toggle_fullscreen, Std.FullScreen),
            'menu.logs': self._action(self.open_logs),
            'menu.about': self._action(self._about, role=Role.AboutRole),
        }
        if self.devtools:
            a['menu.devtools'] = self._action(self._open_devtools, 'F12')

        bar = self.menuBar()
        self.file_menu = bar.addMenu('')
        self.file_menu.addActions([a['menu.home'], a['menu.new'], a['menu.open']])
        self.recent_menu = self.file_menu.addMenu('')
        self.recent_menu.aboutToShow.connect(self._fill_recent)
        self.file_menu.addSeparator()
        self.file_menu.addActions([a['menu.reveal'], a['menu.close']])
        self.file_menu.addSeparator()
        self.file_menu.addAction(a['menu.quit'])

        self.view_menu = bar.addMenu('')
        self.view_menu.addActions([a['menu.reload']])
        self.view_menu.addSeparator()
        self.view_menu.addActions([a['menu.zoomIn'], a['menu.zoomOut'], a['menu.zoomReset']])
        self.view_menu.addSeparator()
        self.view_menu.addAction(a['menu.fullscreen'])
        if self.devtools:
            self.view_menu.addAction(a['menu.devtools'])

        self.help_menu = bar.addMenu('')
        self.help_menu.addActions([a['menu.logs'], a['menu.about']])
        self._retranslate()

    def _retranslate(self):
        for key, action in self.actions_by_key.items():
            action.setText(tr(key, brand=paths.APP_NAME))
        self.file_menu.setTitle(tr('menu.file'))
        self.recent_menu.setTitle(tr('menu.recent'))
        self.view_menu.setTitle(tr('menu.view'))
        self.help_menu.setTitle(tr('menu.help'))
        self._update_title()

    def _fill_recent(self):
        self.recent_menu.clear()
        try:
            items = recent_projects.load(paths.APP_DB)[:15]
        except Exception:
            log.exception("Could not read the recent projects.")
            items = []
        for item in items:
            action = self.recent_menu.addAction(Path(item['path']).name or item['path'])
            action.setToolTip(item['path'])
            action.triggered.connect(lambda _=False, p=item['path']: self.open_project(p))
        if not items:
            empty = self.recent_menu.addAction(tr('menu.recent.empty'))
            empty.setEnabled(False)

    def _update_title(self):
        has_project = bool(self.project)
        for key in ('menu.reveal', 'menu.close'):
            self.actions_by_key[key].setEnabled(has_project)
        self.setWindowTitle(f'{self.project.name} — {paths.APP_NAME}' if has_project
                            else paths.APP_NAME)

    def _zoom(self, step):
        current = self.view.zoomFactor()
        if step == 0:
            factor = 1.0
        elif step > 0:
            factor = next((z for z in ZOOM_STEPS if z > current + 0.01), ZOOM_STEPS[-1])
        else:
            factor = next((z for z in reversed(ZOOM_STEPS) if z < current - 0.01), ZOOM_STEPS[0])
        self.view.setZoomFactor(factor)
        self.settings.setValue('zoom', factor)

    def _toggle_fullscreen(self):
        self.showNormal() if self.isFullScreen() else self.showFullScreen()

    def _open_devtools(self):
        if self.devtools_window is None:
            self.devtools_window = QWebEngineView()
            self.devtools_window.setWindowTitle('DevTools')
            self.devtools_window.resize(1100, 700)
            self.page.setDevToolsPage(self.devtools_window.page())
        self.devtools_window.show()
        self.devtools_window.raise_()

    def _about(self):
        QMessageBox.about(self, tr('menu.about', brand=paths.APP_NAME), tr(
            'about.text', brand=paths.APP_NAME, version=paths.version(),
            data=str(paths.DATA_DIR), logs=str(paths.LOG_DIR)))

    # ------------------------------------------------------------------
    # Janela
    # ------------------------------------------------------------------

    def _restore_geometry(self):
        geometry = self.settings.value('geometry')
        if isinstance(geometry, QByteArray) and self.restoreGeometry(geometry):
            return
        screen = QApplication.primaryScreen().availableGeometry()
        self.resize(int(screen.width() * 0.85), int(screen.height() * 0.85))
        self.move(screen.center() - self.rect().center())

    def closeEvent(self, event):
        if self.export_running():
            box = QMessageBox(QMessageBox.Icon.Warning, tr('quit.exporting.title'),
                              tr('quit.exporting.text'), parent=self)
            close = box.addButton(tr('quit.exporting.confirm'), QMessageBox.ButtonRole.DestructiveRole)
            box.addButton(tr('common.cancel'), QMessageBox.ButtonRole.RejectRole)
            box.exec()
            if box.clickedButton() is not close:
                event.ignore()
                return
        self.settings.setValue('geometry', self.saveGeometry())
        self.shutdown()
        for popup in list(self.popups):
            popup.close()
        if self.devtools_window:
            self.devtools_window.close()
        event.accept()

    def shutdown(self):
        self._stop_server()
        self._release_project()

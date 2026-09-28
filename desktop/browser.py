"""Navegador embarcado (Chromium do Qt WebEngine), endurecido para ser "o app".

O que a pessoa NAO tem, de proposito — nada que lembre um navegador:

* barra de endereco, abas, botoes de voltar/recarregar, menu de contexto;
* DevTools (so com ``--devtools``, em desenvolvimento);
* navegacao para fora do app: link externo abre no navegador padrao do SO,
  qualquer outro esquema (``file:``, ``chrome:``, ``javascript:``) e barrado;
* permissoes de pagina (camera, microfone, localizacao, notificacoes,
  clipboard, tela): todas negadas sem perguntar;
* File System Access API, plugins, soltar arquivo na janela para "abri-lo".

As acoes do app que precisam do SO (escolher pasta, abrir no Finder/Explorer)
chegam como navegacao para ``/__desktop__/<acao>`` — interceptada aqui, antes
de sair da pagina, e entregue ao ``handler`` (``window.py``).
"""
import logging
from pathlib import Path

from PySide6.QtCore import Qt, QTimer, QUrl, QUrlQuery
from PySide6.QtGui import QDesktopServices
from PySide6.QtWebEngineCore import (
    QWebEnginePage, QWebEngineProfile, QWebEngineSettings,
)
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWidgets import QFileDialog, QMainWindow

from desktop import paths

log = logging.getLogger(__name__)

# Prefixo das acoes do app para o shell. Repetido em frontend/src/lib/desktop.js.
ACTION_PREFIX = '/__desktop__/'

# Base das telas locais do shell (carregando, erro): ``setHtml`` precisa de uma
# origem, e esta nunca resolve — nada sai para a rede.
SHELL_BASE = QUrl('http://shell.photoeditor.invalid/')

_Nav = QWebEnginePage.NavigationType
_Attr = QWebEngineSettings.WebAttribute


def create_profile(parent, devtools=False) -> QWebEngineProfile:
    """Perfil PERSISTENTE e proprio do app em ``~/.photoe/webengine``.

    Cookies (idioma), localStorage (preferencias da tela) e cache HTTP das
    miniaturas sobrevivem entre execucoes; nada e compartilhado com os
    navegadores da pessoa.
    """
    profile = QWebEngineProfile(paths.APP_NAME, parent)
    profile.setPersistentStoragePath(str(paths.WEB_PROFILE_DIR / 'storage'))
    profile.setCachePath(str(paths.WEB_PROFILE_DIR / 'cache'))
    profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.DiskHttpCache)
    profile.setHttpCacheMaximumSize(512 * 1024 * 1024)
    profile.setPersistentCookiesPolicy(
        QWebEngineProfile.PersistentCookiesPolicy.AllowPersistentCookies)
    profile.setSpellCheckEnabled(False)
    profile.setHttpUserAgent(f'{profile.httpUserAgent()} {paths.APP_NAME}/{paths.version()}')

    s = profile.settings()
    for attr, on in (
        (_Attr.JavascriptEnabled, True),
        (_Attr.JavascriptCanOpenWindows, True),      # vira janela do app (regra 3)
        (_Attr.JavascriptCanAccessClipboard, False),
        (_Attr.LocalStorageEnabled, True),
        (_Attr.LocalContentCanAccessFileUrls, False),
        (_Attr.LocalContentCanAccessRemoteUrls, False),
        (_Attr.PluginsEnabled, False),
        (_Attr.PdfViewerEnabled, False),
        (_Attr.FullScreenSupportEnabled, False),
        (_Attr.ScreenCaptureEnabled, False),
        (_Attr.WebRTCPublicInterfacesOnly, True),
        (_Attr.DnsPrefetchEnabled, False),
        (_Attr.NavigateOnDropEnabled, False),
        (_Attr.FocusOnNavigationEnabled, True),
        (_Attr.ErrorPageEnabled, devtools),
    ):
        s.setAttribute(attr, on)

    profile.downloadRequested.connect(_on_download)
    return profile


def _on_download(request):
    """Download iniciado pela pagina: sempre pergunta onde salvar."""
    from desktop.window import tr  # textos do shell no idioma atual
    suggested = str(Path(request.downloadDirectory()) / request.downloadFileName())
    target, _ = QFileDialog.getSaveFileName(None, tr('dialog.save.title'), suggested)
    if not target:
        request.cancel()
        return
    request.setDownloadDirectory(str(Path(target).parent))
    request.setDownloadFileName(Path(target).name)
    request.accept()


def same_origin(url: QUrl, origin: QUrl) -> bool:
    return (origin.isValid() and url.scheme() == origin.scheme()
            and url.host() == origin.host() and url.port() == origin.port())


def open_external(url: QUrl):
    """So http(s) e mailto saem para o SO; o resto e barrado."""
    if url.scheme() in ('http', 'https', 'mailto'):
        log.info("Opening external link in the system browser: %s", url.toString())
        QDesktopServices.openUrl(url)
    else:
        log.warning("Blocked navigation to %s", url.toString())


class AppPage(QWebEnginePage):
    """Pagina presa a origem do servidor local."""

    def __init__(self, profile, parent, host):
        super().__init__(profile, parent)
        # ``host``: quem sabe a origem atual e trata as acoes (MainWindow).
        self.host = host
        self.permissionRequested.connect(self._deny_permission)
        self.fileSystemAccessRequested.connect(lambda request: request.reject())

    @staticmethod
    def _deny_permission(permission):
        log.info("Denied page permission %s", permission.permissionType())
        permission.deny()

    def acceptNavigationRequest(self, url, nav_type, is_main_frame):
        origin = self.host.app_origin()
        if same_origin(url, origin):
            if url.path().startswith(ACTION_PREFIX):
                action = url.path()[len(ACTION_PREFIX):].strip('/')
                params = dict(QUrlQuery(url).queryItems(QUrl.ComponentFormattingOption.FullyDecoded))
                self._dispatch(action, params)
                return False
            return True
        # Telas locais do shell (setHtml) e seus links de acao.
        if url.scheme() == 'data' or url.toString() == 'about:blank':
            return is_main_frame and nav_type != _Nav.NavigationTypeLinkClicked
        if url.host() == SHELL_BASE.host():
            if nav_type == _Nav.NavigationTypeLinkClicked:
                self._dispatch(url.path().strip('/'), {})
                return False
            return url == SHELL_BASE
        if nav_type == _Nav.NavigationTypeLinkClicked:
            open_external(url)
        else:
            log.warning("Blocked navigation to %s", url.toString())
        return False

    def _dispatch(self, action, params):
        # Fora deste callback: a acao troca o conteudo da pagina (setHtml,
        # setUrl) e para o servidor; fazer isso DENTRO do
        # acceptNavigationRequest reentra no Chromium e trava a janela.
        QTimer.singleShot(0, lambda: self.host.on_action(action, params))

    def createWindow(self, _type):
        """``window.open`` / ``target=_blank``: uma janela do app, sem barra
        (regra 3 — detalhe abre em nova janela). O destino so e conhecido na
        primeira navegacao, entao quem decide e a ``PopupPage``."""
        return PopupWindow(self.profile(), self.host).page


class PopupPage(AppPage):
    def __init__(self, profile, parent, host, window):
        super().__init__(profile, parent, host)
        self.window = window
        self._decided = False

    def acceptNavigationRequest(self, url, nav_type, is_main_frame):
        if not self._decided and is_main_frame:
            self._decided = True
            if not same_origin(url, self.host.app_origin()):
                # Link externo aberto como "nova janela": vai para o SO.
                open_external(url)
                self.window.close()
                return False
            self.window.show()
        return super().acceptNavigationRequest(url, nav_type, is_main_frame)


class PopupWindow(QMainWindow):
    """Janela secundaria do app: so o conteudo, como a principal."""

    def __init__(self, profile, host):
        super().__init__(None)
        self.view = QWebEngineView(self)
        self.page = PopupPage(profile, self.view, host, self)
        self.view.setPage(self.page)
        host.harden_view(self.view)
        self.setCentralWidget(self.view)
        self.setWindowIcon(host.windowIcon())
        self.resize(1280, 800)
        self.page.titleChanged.connect(self.setWindowTitle)
        self.page.windowCloseRequested.connect(self.close)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose, True)
        # Sem pai Qt: quem segura a referencia Python e o host, ate fechar.
        host.popups.add(self)
        self.destroyed.connect(lambda: host.popups.discard(self))

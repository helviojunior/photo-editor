"""Inicializacao do app desktop: log, Chromium, QApplication e janela.

Uso (o launcher do pacote ja passa ``-E -s``):

    python desktop [PASTA_DO_PROJETO] [--devtools] [--port N]
                   [--frontend-url http://127.0.0.1:3000] [--screenshot ARQ]
"""
import argparse
import logging
import logging.handlers
import os
import sys

from desktop import paths

log = logging.getLogger('desktop')

# Windows: identidade na barra de tarefas (ver main). Nunca mude — atalhos
# fixados pela pessoa apontam para ela.
APP_USER_MODEL_ID = 'PhotoE.PhotoEditor'

# Chromium sem os servicos de fundo de um navegador de verdade: nada de
# telemetria, pings de auditoria ou atualizacao de componentes.
CHROMIUM_FLAGS = (
    '--disable-background-networking',
    '--disable-component-update',
    '--disable-domain-reliability',
    '--disable-breakpad',
    '--no-pings',
)


def setup_logging():
    """``~/.photoe/logs/desktop.log`` (+ terminal, quando houver um)."""
    paths.LOG_DIR.mkdir(parents=True, exist_ok=True)
    handlers = [logging.handlers.RotatingFileHandler(
        paths.LOG_DIR / 'desktop.log', maxBytes=5 * 1024 * 1024, backupCount=3,
        encoding='utf-8')]
    if sys.stderr is not None:
        handlers.append(logging.StreamHandler(sys.stderr))
    logging.basicConfig(
        level=os.environ.get('LOG_LEVEL', 'INFO').upper(), handlers=handlers,
        format='[%(asctime)s] %(levelname)s %(name)s: %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S')


def parse_args(argv):
    parser = argparse.ArgumentParser(prog=paths.APP_NAME)
    parser.add_argument('project', nargs='?', help='event folder to open')
    parser.add_argument('--devtools', action='store_true',
                        help='development: context menu, error pages and DevTools (F12)')
    parser.add_argument('--port', type=int, help='fixed port for the local server')
    parser.add_argument('--frontend-url',
                        help='development: load the React dev server (yarn start) instead of the build')
    parser.add_argument('--debug', action='store_true', help='Django DEBUG=True in the server')
    parser.add_argument('--screenshot', metavar='FILE',
                        help='smoke test: save a screenshot once the page loads, then quit')
    return parser.parse_args(argv)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    # O Finder ainda passa "-psn_0_12345" ao abrir o .app em alguns casos.
    args = parse_args([a for a in argv if not a.startswith('-psn_')])
    setup_logging()
    log.info("%s %s starting (Python %s, %s)", paths.APP_NAME, paths.version(),
             sys.version.split()[0], sys.platform)

    flags = os.environ.get('QTWEBENGINE_CHROMIUM_FLAGS', '').split()
    os.environ['QTWEBENGINE_CHROMIUM_FLAGS'] = ' '.join(dict.fromkeys(flags + list(CHROMIUM_FLAGS)))
    if not args.devtools:
        # Porta de depuracao remota = DevTools pela rede local. Nunca no app.
        os.environ.pop('QTWEBENGINE_REMOTE_DEBUGGING', None)

    if os.name == 'nt':
        # Identidade propria na barra de tarefas: sem isto o Windows agrupa (e
        # fixa) a janela como "pythonw.exe", com o icone do Python.
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_USER_MODEL_ID)

    from PySide6.QtCore import QCoreApplication, QTimer
    from PySide6.QtWidgets import QApplication

    QCoreApplication.setOrganizationName(paths.APP_NAME)
    QCoreApplication.setApplicationName(paths.APP_NAME)
    QCoreApplication.setApplicationVersion(paths.version())
    app = QApplication(sys.argv[:1])
    app.setApplicationDisplayName(paths.APP_NAME)

    log.info("Qt ready")
    from desktop.window import MainWindow

    window = MainWindow(devtools=args.devtools, frontend_url=args.frontend_url,
                        port=args.port, debug=args.debug)
    app.aboutToQuit.connect(window.shutdown)
    window.show()
    window.start(args.project)

    if args.screenshot:
        _arm_screenshot(window, args.screenshot, QTimer)

    code = app.exec()
    log.info("%s exiting (%s)", paths.APP_NAME, code)
    sys.exit(code)


def _arm_screenshot(window, target, QTimer):
    """Smoke test: quando o React terminar de carregar, fotografa e sai."""
    def on_loaded(ok):
        if not ok or window.page.url().host() != '127.0.0.1':
            return
        def shoot():
            window.view.grab().save(target)
            log.info("Screenshot saved to %s", target)
            window.close()
        QTimer.singleShot(4000, shoot)
    window.page.loadFinished.connect(on_loaded)
    QTimer.singleShot(120000, window.close)  # nunca fica pendurado

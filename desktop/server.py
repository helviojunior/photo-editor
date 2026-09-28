"""Servidor local do editor: o Django servido pelo waitress em 127.0.0.1.

Dois lados neste modulo:

* ``ServerProcess`` — usado pelo SHELL (``window.py``) para subir, esperar e
  derrubar o servidor de um projeto (ou do modo Home, sem projeto);
* ``main()`` — o PROCESSO FILHO em si (``python -m desktop.server``).

Processo separado, e nao thread do shell, por tres motivos: trocar de projeto
e matar um processo e subir outro (settings, banco e o estado em memoria da
exportacao sao do projeto); uma falha nativa (onnxruntime, OpenCV) derruba o
servidor e nao a janela; e o processamento de imagem nao disputa o GIL com a
interface.

Tudo chega ao filho por variavel de ambiente — inclusive o token da sessao, que
nunca aparece na linha de comando (visivel a outros usuarios em ``ps``).
"""
import logging
import os
import secrets
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

if not __package__:  # `python desktop/server.py`
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from desktop import paths  # noqa: E402

log = logging.getLogger(__name__)

HOST = '127.0.0.1'
# Porta preferida: fixa para a ORIGEM do app (http://127.0.0.1:<porta>) ser a
# mesma entre execucoes — localStorage e por origem, e as preferencias da tela
# (ordenacao da filmstrip) sumiriam a cada porta nova. Ocupada, sorteia outra.
PREFERRED_PORT = 47823

# Repetidos em backend/photoeditor/middleware.py (quem confere).
TOKEN_COOKIE = 'photoeditor_token'
TOKEN_HEADER = 'X-PhotoEditor-Token'

# Variaveis do SO que nao podem vazar para o filho (outra instalacao de
# Python/Django na maquina mudaria o que ele importa).
_SCRUB_ENV = ('PYTHONPATH', 'PYTHONHOME', 'PYTHONSTARTUP', 'DJANGO_SETTINGS_MODULE')


def _port_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        # Mesmo criterio do waitress (SO_REUSEADDR no POSIX): a porta da
        # execucao anterior, ainda em TIME_WAIT, serve. No Windows a flag
        # permitiria roubar porta EM USO, entao la nao entra.
        if os.name != 'nt':
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((HOST, port))
        except OSError:
            return False
    return True


def pick_port(preferred: int = PREFERRED_PORT) -> int:
    if preferred and _port_free(preferred):
        return preferred
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((HOST, 0))
        return s.getsockname()[1]


class ServerProcess:
    """Um servidor filho. ``project=None`` = modo Home."""

    def __init__(self, project=None, port=None, debug=False):
        self.project = Path(project) if project else None
        self.port = port or pick_port()
        self.token = secrets.token_urlsafe(32)
        self.debug = debug
        self.log_file = paths.LOG_DIR / 'server.log'
        self.proc = None

    @property
    def origin(self) -> str:
        return f'http://{HOST}:{self.port}'

    def start(self):
        env = {k: v for k, v in os.environ.items() if k not in _SCRUB_ENV}
        env.update({
            'PHOTOEDITOR_SERVER_PORT': str(self.port),
            'PHOTOEDITOR_PARENT_PID': str(os.getpid()),
            'APP_TOKEN': self.token,
            'APP_VERSION': paths.version(),
            'DATA_DIR': str(paths.DATA_DIR),
            'APP_DB': str(paths.APP_DB),
            'LOG_FILE': str(self.log_file),
            'FRONTEND_BUILD_DIR': str(paths.FRONTEND_BUILD_DIR),
            'SEGMENT_MODEL_DIR': str(paths.SEGMENT_MODEL_DIR),
            'PROJECT_ROOT': str(self.project) if self.project else '',
            'DEBUG': 'True' if self.debug else 'False',
            'PYTHONUNBUFFERED': '1',
        })
        kwargs = {}
        # Shell sem console (pythonw no Windows, app aberto pelo Finder): o
        # filho escreve so no arquivo de log. Com terminal, herda o terminal.
        if sys.stdout is None:
            kwargs.update(stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if os.name == 'nt':
                kwargs['creationflags'] = subprocess.CREATE_NO_WINDOW
        self.proc = subprocess.Popen(
            [sys.executable, '-E', '-s', '-m', 'desktop.server'],
            cwd=str(paths.APP_ROOT), env=env, stdin=subprocess.DEVNULL, **kwargs)
        log.info("Server started (pid %s) on %s for %s", self.proc.pid, self.origin,
                 self.project or 'Home')

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def stop(self, timeout: float = 5):
        if not self.proc:
            return
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout)
        log.info("Server stopped (pid %s, exit %s)", self.proc.pid, self.proc.returncode)
        self.proc = None


# ---------------------------------------------------------------------------
# Processo filho
# ---------------------------------------------------------------------------

def _parent_watchdog(parent_pid: int):
    """Encerra o servidor se o shell morrer sem derruba-lo (crash, kill -9).

    Sem isso o servidor ficaria orfao segurando a porta e o banco do projeto.
    POSIX: o filho orfao e adotado por outro processo, o ``getppid()`` muda.
    Windows: um handle aberto no inicio espera o processo pai terminar.
    """
    if os.name == 'nt':
        import ctypes
        SYNCHRONIZE = 0x00100000
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(SYNCHRONIZE, False, parent_pid)
        if not handle:
            os._exit(0)
        kernel32.WaitForSingleObject(handle, 0xFFFFFFFF)  # INFINITE
        os._exit(0)
    while os.getppid() == parent_pid:
        time.sleep(1)
    os._exit(0)


def main():
    port = int(os.environ['PHOTOEDITOR_SERVER_PORT'])
    parent_pid = int(os.environ.get('PHOTOEDITOR_PARENT_PID') or 0)
    if parent_pid:
        threading.Thread(target=_parent_watchdog, args=(parent_pid,),
                         name='parent-watchdog', daemon=True).start()

    sys.path.insert(0, str(paths.BACKEND_DIR))
    os.environ['DJANGO_SETTINGS_MODULE'] = 'core.settings'
    # O boot (catalogo) roda abaixo, DEPOIS do migrate — nao no ready().
    from photoeditor.startup import DEFER_ENV
    os.environ[DEFER_ENV] = '1'

    import django
    django.setup()

    from django.conf import settings
    from django.core.management import call_command

    log = logging.getLogger('photoeditor.server')
    log.info("PhotoEditor %s — server for %s", settings.VERSION,
             settings.PROJECT_ROOT or 'Home')

    if not settings.HOME_MODE:
        # Banco do projeto sempre no esquema desta versao do app. So migrate:
        # migration nova vem versionada no pacote (regra 13); gerar migration
        # aqui escreveria na instalacao.
        call_command('migrate', interactive=False, verbosity=0)
        from photoeditor.startup import on_startup
        on_startup(force=True)

    from waitress import create_server
    from core.wsgi import application

    server = create_server(
        application, host=HOST, port=port,
        # Threads: a exportacao roda numa thread e o progresso dela vive na
        # memoria DESTE processo — todas as requisicoes precisam enxerga-lo.
        threads=8, ident='PhotoEditor',
        # Uploads grandes (traco do pincel, recortes) e respostas lentas (render).
        max_request_body_size=200 * 1024 * 1024, channel_timeout=300,
    )
    log.info("Listening on http://%s:%s", HOST, port)
    server.run()


if __name__ == '__main__':
    main()

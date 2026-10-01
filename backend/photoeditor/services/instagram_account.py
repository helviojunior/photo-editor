"""Conta do Instagram conectada ao app (tela Configuracoes > Instagram).

O login e o do app do celular, com usuario e senha, pela API privada
(``instagrapi``): a API oficial da Meta so aceita fotos por URL publica, e o
app roda em 127.0.0.1. O preco e o de toda API nao oficial: o Instagram pode
pedir confirmacao, bloquear por um tempo ou mudar o protocolo.

O que fica guardado e a SESSAO (cookies e identificadores do "aparelho"),
nunca a senha, em ``~/.photoe/instagram.json`` (permissao 0600) — e da
maquina, vale para todos os eventos. Junto vai o modelo da legenda do
"Publicar no Instagram". Sessao expirada = conectar de novo.

O login pode parar no meio para pedir um codigo (verificacao em duas etapas
ou o "confirme que e voce" do Instagram). O ``instagrapi`` pede esse codigo
de forma SINCRONA, entao o login roda numa thread (``LoginJob``) que espera o
codigo chegar por outra requisicao (``submit_code``). O servidor local e um
processo so (waitress com threads): todas as requisicoes enxergam o mesmo job.
"""
import json
import logging
import os
import tempfile
import threading
import time

from django.conf import settings
from django.utils import timezone

from photoeditor.services.history import ActionError

log = logging.getLogger(__name__)

# Quanto uma requisicao espera o login andar (concluir, falhar ou pedir
# codigo) antes de responder "ainda em andamento" — o frontend consulta de novo.
WAIT_SECONDS = 40
# Quanto o login espera o codigo digitado antes de desistir.
CODE_TIMEOUT = 600
# Pausa entre as chamadas a API privada (o app do celular nao dispara em rajada).
DELAY_RANGE = [1, 3]

_file_lock = threading.Lock()
_job_lock = threading.Lock()
_job = None


# --------------------------------------------------------------------------- #
# Arquivo da conta
# --------------------------------------------------------------------------- #

def _read() -> dict:
    path = settings.INSTAGRAM_ACCOUNT_FILE
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except FileNotFoundError:
        return {}
    except (OSError, ValueError):
        log.warning("Could not read %s; treating the account as disconnected.", path,
                    exc_info=True)
        return {}
    return data if isinstance(data, dict) else {}


def _write(data: dict):
    """Grava atomico e so para o dono (0600): a sessao vale como a senha."""
    path = settings.INSTAGRAM_ACCOUNT_FILE
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix='.instagram-', suffix='.tmp')
    try:
        os.chmod(tmp, 0o600)
        with os.fdopen(fd, 'w', encoding='utf-8') as f:
            json.dump(data, f)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def _update(**fields):
    with _file_lock:
        data = _read()
        for key, value in fields.items():
            if value is None:
                data.pop(key, None)
            else:
                data[key] = value
        _write(data)


def status() -> dict:
    """O que a tela de configuracao e o modal de publicar precisam."""
    data = _read()
    with _job_lock:
        job = _job.snapshot() if _job is not None else None
    return {
        'connected': bool(data.get('session') and data.get('username')),
        'username': data.get('username') or '',
        'connected_at': data.get('connected_at'),
        'caption_template': data.get('caption_template'),
        'login': job,
    }


def set_caption_template(text):
    """Modelo da legenda (``None`` ou vazio = o padrao do idioma)."""
    text = (text or '').strip()
    _update(caption_template=text[:2200] or None)


def disconnect():
    """Esquece a sessao. O modelo da legenda fica."""
    cancel_login()
    _update(session=None, username=None, connected_at=None)
    log.info("Instagram account disconnected.")


def client():
    """``(Client com a sessao salva, usuario)``. Sem conta: ``ActionError``."""
    from instagrapi import Client

    data = _read()
    if not (data.get('session') and data.get('username')):
        raise ActionError('instagram.notConnected')
    cl = Client()
    cl.delay_range = DELAY_RANGE
    cl.set_settings(data['session'])
    cl.username = data['username']
    return cl, data['username']


def save_session(cl):
    """Regrava a sessao depois de usar o cliente (os cookies se renovam)."""
    with _file_lock:
        data = _read()
        if data.get('username'):
            data['session'] = cl.get_settings()
            _write(data)


# --------------------------------------------------------------------------- #
# Login
# --------------------------------------------------------------------------- #

class _Cancelled(Exception):
    pass


class LoginJob:
    """Um login em andamento. ``state``: ``running`` | ``code`` (esperando o
    codigo; ``kind`` = ``two_factor`` ou ``challenge``) | ``done`` | ``error``
    | ``cancelled``."""

    def __init__(self, username, password):
        self.username = username
        self._password = password
        self._cond = threading.Condition()
        self._code = None
        self._cancelled = False
        self.state = 'running'
        self.kind = ''
        self.error = ''
        self.detail = ''
        self._thread = threading.Thread(target=self._run, name='instagram-login', daemon=True)

    def start(self):
        self._thread.start()

    def snapshot(self) -> dict:
        with self._cond:
            return {'state': self.state, 'kind': self.kind, 'username': self.username,
                    'error': self.error, 'detail': self.detail}

    @property
    def finished(self):
        return self.state in ('done', 'error', 'cancelled')

    def _set(self, **fields):
        with self._cond:
            for key, value in fields.items():
                setattr(self, key, value)
            self._cond.notify_all()

    def wait(self, timeout=WAIT_SECONDS):
        """Espera o login sair de ``running`` (ou o tempo acabar)."""
        deadline = time.monotonic() + timeout
        with self._cond:
            while self.state == 'running':
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                self._cond.wait(left)

    def submit_code(self, code):
        with self._cond:
            if self.state != 'code':
                raise ActionError('instagram.noCodePending')
            self._code = code
            self.state = 'running'
            self._cond.notify_all()

    def cancel(self):
        with self._cond:
            self._cancelled = True
            self._cond.notify_all()

    def _wait_code(self, kind) -> str:
        """Chamado DENTRO do login: pede o codigo e bloqueia ate ele chegar."""
        deadline = time.monotonic() + CODE_TIMEOUT
        with self._cond:
            self._code = None
            self.state, self.kind = 'code', kind
            self._cond.notify_all()
            while self._code is None and not self._cancelled:
                left = deadline - time.monotonic()
                if left <= 0:
                    break
                self._cond.wait(left)
            if self._cancelled:
                raise _Cancelled()
            code, self._code = self._code or '', None
            if self.state == 'code':
                self.state = 'running'
            return code

    def _run(self):
        from instagrapi import Client
        from instagrapi import exceptions as ig

        cl = Client()
        cl.delay_range = DELAY_RANGE
        # "Confirme que e voce": o instagrapi chama isto e espera o codigo
        # (enviado por e-mail ou SMS) como retorno.
        cl.challenge_code_handler = lambda username, choice: self._wait_code('challenge')
        try:
            try:
                cl.login(self.username, self._password)
            except ig.TwoFactorRequired:
                code = self._wait_code('two_factor')
                if not code:
                    raise ig.TwoFactorRequired('no code') from None
                cl.login(self.username, self._password, verification_code=code)
            with _file_lock:
                data = _read()
                data.update(username=self.username, session=cl.get_settings(),
                            connected_at=timezone.now().isoformat())
                _write(data)
            log.info("Instagram account connected: @%s", self.username)
            self._set(state='done', kind='')
        except _Cancelled:
            log.info("Instagram login cancelled (@%s).", self.username)
            self._set(state='cancelled', kind='')
        except Exception as exc:
            key = error_key(exc)
            log.warning("Instagram login failed (@%s): %s: %s", self.username,
                        type(exc).__name__, exc)
            self._set(state='error', kind='', error=key, detail=_detail(exc, key))
        finally:
            self._password = None


def error_key(exc) -> str:
    """Chave de i18n para um erro do ``instagrapi``."""
    from instagrapi import exceptions as ig

    if isinstance(exc, ActionError):
        return exc.key
    if isinstance(exc, (ig.BadPassword, ig.BadCredentials)):
        return 'instagram.badCredentials'
    if isinstance(exc, ig.TwoFactorRequired):
        return 'instagram.badCode'
    if isinstance(exc, ig.ChallengeError):
        return 'instagram.challenge'
    if isinstance(exc, (ig.LoginRequired, ig.ClientLoginRequired, ig.ReloginAttemptExceeded)):
        return 'instagram.sessionExpired'
    if isinstance(exc, (ig.PleaseWaitFewMinutes, ig.FeedbackRequired,
                        ig.ClientThrottledError)):
        return 'instagram.wait'
    if isinstance(exc, ig.AccountSuspended):
        return 'instagram.suspended'
    if isinstance(exc, (ig.ClientConnectionError, ig.ClientRequestTimeout, OSError)):
        return 'instagram.network'
    return 'instagram.failed'


def _detail(exc, key) -> str:
    """Texto do Instagram para os erros sem mensagem propria."""
    if key not in ('instagram.failed', 'instagram.challenge'):
        return ''
    return str(exc).strip()[:300]


def start_login(username, password) -> dict:
    """Comeca um login (cancelando um anterior) e espera ele andar."""
    global _job
    username = (username or '').strip().lstrip('@')
    if not username or not password:
        raise ActionError('instagram.missingCredentials')
    with _job_lock:
        if _job is not None and not _job.finished:
            _job.cancel()
        _job = LoginJob(username, password)
        job = _job
    job.start()
    job.wait()
    return status()


def submit_code(code) -> dict:
    code = ''.join(ch for ch in str(code or '') if ch.isalnum())
    if not code:
        raise ActionError('instagram.missingCode')
    with _job_lock:
        job = _job
    if job is None:
        raise ActionError('instagram.noCodePending')
    job.submit_code(code)
    job.wait()
    return status()


def poll_login() -> dict:
    """Espera um pouco o login em andamento andar (o frontend consulta em laco)."""
    with _job_lock:
        job = _job
    if job is not None and not job.finished:
        job.wait()
    return status()


def cancel_login():
    global _job
    with _job_lock:
        job, _job = _job, None
    if job is not None and not job.finished:
        job.cancel()

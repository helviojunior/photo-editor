#!/usr/bin/env python3
"""Ajudante do Instagram do PhotoE: a unica parte do app que continua em Python.

O app nativo (C++/Qt) sobe este processo com ``QProcess``::

    python -I -E -s photoe_instagram.py --data-dir <~/.photoe>

e conversa com ele por JSON, UM OBJETO POR LINHA (UTF-8), sem servidor HTTP:

* pedido (stdin):   ``{"id": 1, "cmd": "status", "args": {}}``
* resposta (stdout): ``{"id": 1, "ok": true, "result": {...}}`` ou
  ``{"id": 1, "ok": false, "error": "<chave i18n>", "detail": "...", "params": {...}}``
* evento (stdout, sem ``id``): ``{"event": "login" | "publish", ...}``

Fim do stdin (EOF) = o app fechou: o processo sai.

O stdout e o canal do protocolo: nada mais pode sair nele. Por isso, no boot,
o descritor 1 original e duplicado para uso exclusivo do protocolo e o 1 passa
a apontar para o stderr — ``print()`` perdido (nosso ou de biblioteca) vai
para o stderr e nao corrompe o canal.

O login e o do app do celular (API privada, ``instagrapi``). Fica guardada a
SESSAO, nunca a senha, em ``<data-dir>/instagram.json`` (0600), no mesmo
formato que o backend Django gravava (``username``, ``session``,
``connected_at``, ``caption_template``) — quem ja estava conectado continua.

Com ``PHOTOE_INSTAGRAM_FAKE=1`` o ``instagrapi`` nao e importado: entra um
cliente falso (``FakeClient``) para testar o protocolo sem rede.
"""
import argparse
import json
import logging
import logging.handlers
import os
import re
import sys
import tempfile
import threading
import time
import types
from datetime import datetime, timezone
from pathlib import Path

log = logging.getLogger('photoe.instagram')

PROTOCOL_VERSION = 1

# Quanto o login espera o codigo digitado antes de desistir.
CODE_TIMEOUT = 600
# Pausa entre as chamadas a API privada (o app do celular nao dispara em rajada).
DELAY_RANGE = [1, 3]
# Limite do carrossel na API do app (o mesmo da API oficial).
MAX_PHOTOS = 10
# Limite da legenda no Instagram.
CAPTION_MAX = 2200
# Modelo padrao da legenda (o mesmo de instagram.captionTemplate no frontend).
DEFAULT_TEMPLATE = '{event}\n{date}\n\n{hashtag}'

FAKE = os.environ.get('PHOTOE_INSTAGRAM_FAKE') == '1'


class ActionError(Exception):
    """Erro com chave de i18n (e parametros dela) que vai para o app."""

    def __init__(self, key, detail='', **params):
        super().__init__(key)
        self.key = key
        self.detail = detail
        self.params = params


# --------------------------------------------------------------------------- #
# Canal do protocolo
# --------------------------------------------------------------------------- #

_out_lock = threading.Lock()
_out = None  # arquivo binario do stdout ORIGINAL (so o protocolo escreve nele)


def _take_stdout():
    """Reserva o stdout para o protocolo e manda o resto para o stderr."""
    global _out
    sys.stdout.flush()
    _out = os.fdopen(os.dup(1), 'wb', buffering=0)
    os.dup2(2, 1)  # escrita direta no fd 1 (codigo C, subprocessos) cai no stderr
    sys.stdout = sys.stderr


def send(obj):
    """Uma linha JSON no canal. Seguro entre threads."""
    line = json.dumps(obj, ensure_ascii=False, separators=(',', ':')).encode('utf-8') + b'\n'
    with _out_lock:
        try:
            _out.write(line)
            _out.flush()
        except (BrokenPipeError, OSError, ValueError):
            # O app sumiu: nao ha a quem responder.
            pass


def emit(event, **fields):
    send({'event': event, **fields})


# --------------------------------------------------------------------------- #
# Log: stderr + <data-dir>/logs/instagram.log, sem senha nem cookie
# --------------------------------------------------------------------------- #

_SECRET_PATTERNS = [
    # Token do cabecalho Authorization (antes do padrao generico, que so
    # pegaria a palavra "Bearer").
    re.compile(r'(?i)(Bearer\s+IGT:\d+:)[A-Za-z0-9+/=_-]+'),
    # "sessionid": "..."  /  'password': '...'  /  enc_password=...
    re.compile(r'''(?i)(["']?\b(?:password|enc_password|sessionid|csrftoken|ds_user_id|'''
               r'''authorization|cookie|mid|rur|ig_did|x-mid|x-ig-www-claim|'''
               r'''verification_code|security_code)\b["']?\s*[:=]\s*)(["']?)[^"',;&\s}]+'''),
]


class _RedactFilter(logging.Filter):
    """Ultima barreira: apaga o que parecer senha, cookie ou token."""

    def filter(self, record):
        try:
            msg = record.getMessage()
        except Exception:
            return True
        clean = msg
        for pattern in _SECRET_PATTERNS:
            clean = pattern.sub(lambda m: m.group(1) + (m.group(2) if m.re.groups >= 2 else '')
                                + '***', clean)
        if clean != msg:
            record.msg, record.args = clean, None
        return True


def setup_logging(data_dir: Path):
    level = os.environ.get('LOG_LEVEL', 'INFO').upper()
    fmt = logging.Formatter('%(asctime)s %(levelname)s %(name)s [%(threadName)s] %(message)s')
    root = logging.getLogger()
    root.setLevel(level)
    redact = _RedactFilter()

    console = logging.StreamHandler(sys.stderr)
    console.setFormatter(fmt)
    console.addFilter(redact)
    root.addHandler(console)

    try:
        logs = data_dir / 'logs'
        logs.mkdir(parents=True, exist_ok=True)
        handler = logging.handlers.RotatingFileHandler(
            logs / 'instagram.log', maxBytes=5 * 1024 * 1024, backupCount=3, encoding='utf-8')
        handler.setFormatter(fmt)
        handler.addFilter(redact)
        root.addHandler(handler)
    except OSError:
        log.warning("Could not open the log file in %s; logging to stderr only.", data_dir,
                    exc_info=True)

    # O instagrapi registra pedidos/respostas inteiros (com cookies e o
    # enc_password) em DEBUG/INFO: nunca abaixo de WARNING.
    for name in ('instagrapi', 'private_request', 'public_request', 'graphql_request',
                 'urllib3', 'requests', 'curl_cffi'):
        logging.getLogger(name).setLevel(logging.WARNING)


# --------------------------------------------------------------------------- #
# Cliente: o instagrapi de verdade ou o falso dos testes
# --------------------------------------------------------------------------- #

def _fake_exceptions():
    """Mesmos nomes do ``instagrapi.exceptions`` que o ``error_key`` usa."""
    ns = types.SimpleNamespace()
    ns.ClientError = type('ClientError', (Exception,), {})
    ns.PrivateError = type('PrivateError', (ns.ClientError,), {})
    for name in ('BadPassword', 'BadCredentials', 'TwoFactorRequired', 'ChallengeError',
                 'LoginRequired', 'PleaseWaitFewMinutes', 'FeedbackRequired'):
        setattr(ns, name, type(name, (ns.PrivateError,), {}))
    for name in ('ClientLoginRequired', 'ReloginAttemptExceeded', 'ClientThrottledError',
                 'AccountSuspended', 'ClientConnectionError', 'ClientRequestTimeout'):
        setattr(ns, name, type(name, (ns.ClientError,), {}))
    return ns


_ig_cache = None


def ig_exceptions():
    global _ig_cache
    if _ig_cache is None:
        if FAKE:
            _ig_cache = _fake_exceptions()
        else:
            from instagrapi import exceptions
            _ig_cache = exceptions
    return _ig_cache


class FakeClient:
    """Cliente falso (``PHOTOE_INSTAGRAM_FAKE=1``), comandado pelo usuario:

    * ``fake_2fa``       pede codigo de duas etapas; aceita ``123456``
    * ``fake_challenge`` pede o "confirme que e voce"; aceita ``654321``
    * ``fake_bad``       senha errada
    * ``fake_wait``      Instagram limitando (``instagram.wait``)
    * ``fake_slow``      demora 2 s para responder (testa cancelar)
    * qualquer outro     conecta direto

    No envio, arquivo com ``fail`` no nome falha.
    """

    def __init__(self):
        self.delay_range = DELAY_RANGE
        self.challenge_code_handler = None
        self.username = None
        self._settings = {}

    def login(self, username, password, relogin=False, verification_code=''):
        ig = ig_exceptions()
        # Bibliotecas as vezes imprimem no stdout: o canal tem de sobreviver.
        print('fake client: stray print')
        os.write(1, b'fake client: stray write on fd 1\n')
        if username == 'fake_bad':
            raise ig.BadPassword('The password you entered is incorrect.')
        if username == 'fake_wait':
            raise ig.PleaseWaitFewMinutes('Please wait a few minutes before you try again.')
        if username == 'fake_slow':
            time.sleep(2)
        if username == 'fake_2fa':
            if not verification_code:
                raise ig.TwoFactorRequired('Two-factor authentication required')
            if verification_code != '123456':
                raise ig.TwoFactorRequired('Invalid code')
        if username == 'fake_challenge':
            code = self.challenge_code_handler(username, 1)
            if code != '654321':
                raise ig.ChallengeError('Challenge code rejected')
        self.username = username
        self._settings = {'uuids': {'phone_id': 'fake'},
                          'authorization_data': {'ds_user_id': '1', 'sessionid': 'fake'},
                          'cookies': {}, 'user_agent': 'fake'}
        return True

    def get_settings(self):
        return dict(self._settings)

    def set_settings(self, data):
        self._settings = dict(data)

    def _media(self, n):
        return types.SimpleNamespace(pk=f'{3000000000000000000 + n}', code=f'FAKE{n}')

    def photo_upload(self, path, caption):
        return self.album_upload([path], caption)

    def album_upload(self, paths, caption):
        time.sleep(0.05)
        if any('fail' in Path(p).name for p in paths):
            raise ig_exceptions().ClientError('fake upload failure')
        return self._media(len(paths))


def new_client():
    if FAKE:
        cl = FakeClient()
    else:
        from instagrapi import Client
        cl = Client()
    cl.delay_range = DELAY_RANGE
    return cl


def error_key(exc) -> str:
    """Chave de i18n para um erro do ``instagrapi`` (a mesma do backend antigo)."""
    if isinstance(exc, ActionError):
        return exc.key
    ig = ig_exceptions()
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
    if key not in ('instagram.failed', 'instagram.challenge', 'instagram.publishFailed'):
        return ''
    return str(exc).strip()[:300]


# --------------------------------------------------------------------------- #
# Arquivo da conta (<data-dir>/instagram.json, 0600)
# --------------------------------------------------------------------------- #

class Account:
    def __init__(self, path: Path):
        self.path = path
        self.lock = threading.Lock()

    def read(self) -> dict:
        try:
            data = json.loads(self.path.read_text(encoding='utf-8'))
        except FileNotFoundError:
            return {}
        except (OSError, ValueError):
            log.warning("Could not read %s; treating the account as disconnected.", self.path)
            return {}
        return data if isinstance(data, dict) else {}

    def _write(self, data: dict):
        """Grava atomico e so para o dono (0600): a sessao vale como a senha."""
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=self.path.parent, prefix='.instagram-', suffix='.tmp')
        try:
            os.chmod(tmp, 0o600)
            with os.fdopen(fd, 'w', encoding='utf-8') as f:
                json.dump(data, f)
            os.replace(tmp, self.path)
        except BaseException:
            try:
                os.unlink(tmp)
            except OSError:
                pass
            raise

    def update(self, **fields):
        """``None`` remove a chave."""
        with self.lock:
            data = self.read()
            for key, value in fields.items():
                if value is None:
                    data.pop(key, None)
                else:
                    data[key] = value
            self._write(data)

    def save_session(self, cl):
        """Regrava a sessao depois de usar o cliente (os cookies se renovam)."""
        with self.lock:
            data = self.read()
            if data.get('username'):
                data['session'] = cl.get_settings()
                self._write(data)

    def client(self):
        """``(cliente com a sessao salva, usuario)``. Sem conta: ``ActionError``."""
        data = self.read()
        if not (data.get('session') and data.get('username')):
            raise ActionError('instagram.notConnected')
        cl = new_client()
        cl.set_settings(data['session'])
        cl.username = data['username']
        return cl, data['username']


# --------------------------------------------------------------------------- #
# Login
# --------------------------------------------------------------------------- #

class _Cancelled(Exception):
    pass


class LoginJob:
    """Um login em andamento. ``state``: ``running`` | ``code`` (esperando o
    codigo; ``kind`` = ``two_factor`` ou ``challenge``) | ``done`` | ``error``
    | ``cancelled``. Cada mudanca de estado sai como evento ``login``; o
    ``login_id`` separa os eventos de um login dos de outro ja abandonado."""

    FINAL = ('done', 'error', 'cancelled')

    def __init__(self, login_id, username, password, account: Account):
        self.login_id = login_id
        self.username = username
        self._password = password
        self._account = account
        self._cond = threading.Condition()
        self._code = None
        self._cancelled = False
        self.state = 'running'
        self.kind = ''
        self.error = ''
        self.detail = ''
        self._thread = threading.Thread(target=self._run, name=f'login-{login_id}', daemon=True)

    def start(self):
        self._emit()
        self._thread.start()

    def snapshot(self) -> dict:
        with self._cond:
            return self._snapshot()

    def _snapshot(self):
        return {'login_id': self.login_id, 'state': self.state, 'kind': self.kind,
                'username': self.username, 'error': self.error, 'detail': self.detail}

    @property
    def finished(self):
        return self.state in self.FINAL

    def _emit(self):
        emit('login', **self.snapshot())

    def _set(self, **fields):
        """Muda o estado e avisa — exceto se o login ja terminou (um cancelar
        que chegou antes vence o resultado tardio da thread)."""
        with self._cond:
            if self.finished:
                return False
            for key, value in fields.items():
                setattr(self, key, value)
            self._cond.notify_all()
        self._emit()
        return True

    def submit_code(self, code):
        with self._cond:
            if self.state != 'code':
                raise ActionError('instagram.noCodePending')
            self._code = code
            self.state, self.kind = 'running', ''
            self._cond.notify_all()
        self._emit()

    def cancel(self):
        with self._cond:
            self._cancelled = True
            self._cond.notify_all()
        # O instagrapi nao interrompe uma requisicao em voo: o estado vira
        # "cancelled" agora e o resultado que a thread trouxer depois e
        # descartado (nem a sessao e gravada).
        self._set(state='cancelled', kind='', error='', detail='')

    def _wait_code(self, kind) -> str:
        """Chamado DENTRO do login: pede o codigo e bloqueia ate ele chegar."""
        deadline = time.monotonic() + CODE_TIMEOUT
        with self._cond:
            if self._cancelled:
                raise _Cancelled()
            self._code = None
            self.state, self.kind = 'code', kind
            self._cond.notify_all()
        self._emit()
        with self._cond:
            while self._code is None and not self._cancelled:
                left = deadline - time.monotonic()
                if left <= 0:
                    log.info("Instagram login code timed out (@%s).", self.username)
                    break
                self._cond.wait(left)
            if self._cancelled:
                raise _Cancelled()
            code, self._code = self._code or '', None
            if self.state == 'code':
                self.state, self.kind = 'running', ''
            return code

    def _run(self):
        ig = ig_exceptions()
        cl = new_client()
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
            with self._cond:
                if self._cancelled:
                    raise _Cancelled()
            self._account.update(username=self.username, session=cl.get_settings(),
                                 connected_at=datetime.now(timezone.utc).isoformat())
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


# --------------------------------------------------------------------------- #
# Publicacao
# --------------------------------------------------------------------------- #

class Publisher:
    def __init__(self, account: Account):
        self._account = account
        self._lock = threading.Lock()
        self._running = False
        self._seq = 0

    def start(self, files, caption) -> dict:
        if not isinstance(files, list) or not all(isinstance(f, str) for f in files):
            raise ActionError('protocol.badRequest', detail='files must be a list of paths')
        files = list(dict.fromkeys(f for f in files if f))
        if not files:
            raise ActionError('instagram.noPhotos')
        if len(files) > MAX_PHOTOS:
            raise ActionError('instagram.tooMany', max=MAX_PHOTOS)
        paths = [Path(f) for f in files]
        for p in paths:
            if not p.is_absolute() or not p.is_file():
                raise ActionError('instagram.publishFailed', detail=f'File not found: {p}')
        caption = (caption or '').strip() if isinstance(caption, str) else ''
        if len(caption) > CAPTION_MAX:
            raise ActionError('instagram.captionTooLong', max=CAPTION_MAX)
        # Ja falha aqui se nao ha conta (antes de responder "comecou").
        cl, username = self._account.client()
        with self._lock:
            if self._running:
                raise ActionError('instagram.publishRunning')
            self._running = True
            self._seq += 1
            publish_id = self._seq
        threading.Thread(target=self._run, args=(publish_id, cl, username, paths, caption),
                         name=f'publish-{publish_id}', daemon=True).start()
        return {'publish_id': publish_id, 'total': len(paths), 'username': username}

    def running(self):
        with self._lock:
            return self._running

    def _run(self, publish_id, cl, username, paths, caption):
        base = {'publish_id': publish_id}
        # O evento terminal (done/error) e emitido SO depois de liberar
        # ``_running``: assim o cliente pode mandar o proximo post (varios posts
        # de um envio de 10+ fotos) assim que recebe o 'done', sem corrida com o
        # 'instagram.publishRunning'.
        terminal = None
        try:
            emit('publish', **base, state='running', phase='upload', total=len(paths))
            log.info("Publishing %d photo(s) to @%s", len(paths), username)
            if len(paths) == 1:
                media = cl.photo_upload(paths[0], caption)
            else:
                media = cl.album_upload(paths, caption)
            self._account.save_session(cl)
            code = getattr(media, 'code', '') or ''
            media_id = str(getattr(media, 'pk', '') or '')
            url = f'https://www.instagram.com/p/{code}/' if code else ''
            log.info("Published to @%s: %s", username, url or media_id)
            terminal = dict(state='done', media_id=media_id, code=code, url=url, username=username)
        except Exception as exc:
            key = error_key(exc)
            if key == 'instagram.failed':
                key = 'instagram.publishFailed'
                log.exception("Instagram publish failed")
            else:
                log.warning("Instagram publish failed: %s: %s", type(exc).__name__, exc)
            terminal = dict(state='error', error=key, detail=_detail(exc, key))
        finally:
            with self._lock:
                self._running = False
        emit('publish', **base, **terminal)


# --------------------------------------------------------------------------- #
# Comandos
# --------------------------------------------------------------------------- #

class Helper:
    def __init__(self, data_dir: Path):
        self.account = Account(data_dir / 'instagram.json')
        self.publisher = Publisher(self.account)
        self._job_lock = threading.Lock()
        self._job = None
        self._login_seq = 0

    def _job_snapshot(self):
        with self._job_lock:
            job = self._job
        return job.snapshot() if job is not None else None

    def cmd_ping(self, args):
        return {'pong': True, 'protocol': PROTOCOL_VERSION, 'fake': FAKE}

    def cmd_status(self, args):
        data = self.account.read()
        return {
            'connected': bool(data.get('session') and data.get('username')),
            'username': data.get('username') or '',
            'connected_at': data.get('connected_at'),
            'caption_template': data.get('caption_template'),
            'default_template': DEFAULT_TEMPLATE,
            'max_photos': MAX_PHOTOS,
            'caption_max': CAPTION_MAX,
            'login': self._job_snapshot(),
            'publishing': self.publisher.running(),
        }

    def cmd_set_template(self, args):
        """Modelo da legenda (``None`` ou vazio = o padrao)."""
        text = args.get('text')
        text = text.strip() if isinstance(text, str) else ''
        self.account.update(caption_template=text[:CAPTION_MAX] or None)
        return self.cmd_status(args)

    def cmd_logout(self, args):
        """Esquece a sessao. O modelo da legenda fica."""
        self._cancel_job()
        self.account.update(session=None, username=None, connected_at=None)
        log.info("Instagram account disconnected.")
        return self.cmd_status(args)

    def cmd_login(self, args):
        username = args.get('username')
        password = args.get('password')
        username = username.strip().lstrip('@') if isinstance(username, str) else ''
        if not username or not isinstance(password, str) or not password:
            raise ActionError('instagram.missingCredentials')
        with self._job_lock:
            old = self._job
            self._login_seq += 1
            job = LoginJob(self._login_seq, username, password, self.account)
            self._job = job
        if old is not None and not old.finished:
            old.cancel()
        job.start()
        return job.snapshot()

    def cmd_code(self, args):
        code = ''.join(ch for ch in str(args.get('code') or '') if ch.isalnum())
        if not code:
            raise ActionError('instagram.missingCode')
        with self._job_lock:
            job = self._job
        if job is None:
            raise ActionError('instagram.noCodePending')
        job.submit_code(code)
        return job.snapshot()

    def _cancel_job(self):
        with self._job_lock:
            job, self._job = self._job, None
        if job is not None and not job.finished:
            job.cancel()
        return job

    def cmd_cancel(self, args):
        job = self._cancel_job()
        return {'login': job.snapshot() if job is not None else None}

    def cmd_publish(self, args):
        return self.publisher.start(args.get('files'), args.get('caption'))

    def handle(self, line: bytes):
        try:
            req = json.loads(line.decode('utf-8'))
        except (UnicodeDecodeError, ValueError) as exc:
            send({'id': None, 'ok': False, 'error': 'protocol.badRequest',
                  'detail': f'Invalid JSON: {exc}'[:300]})
            return
        if not isinstance(req, dict):
            send({'id': None, 'ok': False, 'error': 'protocol.badRequest',
                  'detail': 'Request must be a JSON object'})
            return
        req_id = req.get('id')
        cmd = req.get('cmd')
        args = req.get('args') or {}
        handler = getattr(self, f'cmd_{cmd}', None) if isinstance(cmd, str) else None
        if handler is None or not isinstance(args, dict):
            send({'id': req_id, 'ok': False,
                  'error': 'protocol.badRequest' if handler else 'protocol.unknownCommand',
                  'detail': f'Unknown command: {cmd!r}' if handler is None else 'args must be an object'})
            return
        # Nunca registrar ``args`` inteiro: o login leva a senha.
        log.debug("Command %s (id=%s)", cmd, req_id)
        try:
            result = handler(args)
        except ActionError as exc:
            reply = {'id': req_id, 'ok': False, 'error': exc.key, 'detail': exc.detail}
            if exc.params:
                reply['params'] = exc.params
            send(reply)
            return
        except Exception as exc:
            log.exception("Command %s failed", cmd)
            send({'id': req_id, 'ok': False, 'error': 'instagram.failed',
                  'detail': f'{type(exc).__name__}: {exc}'[:300]})
            return
        send({'id': req_id, 'ok': True, 'result': result})


def main(argv=None):
    parser = argparse.ArgumentParser(description='PhotoE Instagram helper (JSON lines on stdio).')
    parser.add_argument('--data-dir', required=True, help='Machine data folder (~/.photoe).')
    opts = parser.parse_args(argv)

    _take_stdout()
    data_dir = Path(opts.data_dir).expanduser()
    data_dir.mkdir(parents=True, exist_ok=True)
    setup_logging(data_dir)
    log.info("Instagram helper started (pid %d, protocol %d%s).", os.getpid(), PROTOCOL_VERSION,
             ', FAKE client' if FAKE else '')

    helper = Helper(data_dir)
    stdin = sys.stdin.buffer
    while True:
        line = stdin.readline()
        if not line:
            break  # EOF: o app fechou o canal
        line = line.strip()
        if line:
            helper.handle(line)
    log.info("stdin closed; Instagram helper exiting.")
    logging.shutdown()
    return 0


if __name__ == '__main__':
    sys.exit(main())

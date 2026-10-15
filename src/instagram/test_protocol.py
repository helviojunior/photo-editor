"""Testes do protocolo do ajudante do Instagram, sem rede.

Sobe o ``photoe_instagram.py`` de verdade (processo filho) com
``PHOTOE_INSTAGRAM_FAKE=1`` e conversa com ele por stdin/stdout, como o app
C++ faz.

    python -m unittest -v
"""
import json
import os
import queue
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / 'photoe_instagram.py'
TIMEOUT = 10


class Helper:
    """Processo do ajudante + leitor de linhas em thread."""

    def __init__(self, data_dir):
        env = dict(os.environ, PHOTOE_INSTAGRAM_FAKE='1')
        self.proc = subprocess.Popen(
            [sys.executable, '-I', '-E', '-s', str(SCRIPT), '--data-dir', str(data_dir)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self.lines = queue.Queue()
        self.raw = []
        self._next = 0
        self._pending = []  # mensagens lidas mas ainda nao consumidas
        threading.Thread(target=self._reader, daemon=True).start()
        self.stderr = []
        threading.Thread(target=self._err_reader, daemon=True).start()

    def _reader(self):
        for line in self.proc.stdout:
            self.raw.append(line)
            self.lines.put(json.loads(line))
        self.lines.put(None)

    def _err_reader(self):
        for line in self.proc.stderr:
            self.stderr.append(line.decode('utf-8', 'replace'))

    def send_raw(self, data: bytes):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def call(self, cmd, **args):
        self._next += 1
        rid = self._next
        self.send_raw(json.dumps({'id': rid, 'cmd': cmd, 'args': args}).encode() + b'\n')
        return self.wait(lambda m: m.get('id') == rid and 'ok' in m)

    def wait(self, pred, timeout=TIMEOUT):
        for i, msg in enumerate(self._pending):
            if pred(msg):
                return self._pending.pop(i)
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise AssertionError('timeout waiting for message; pending=%r' % self._pending)
            msg = self.lines.get(timeout=left)
            if msg is None:
                raise AssertionError('helper closed stdout; stderr=%s' % ''.join(self.stderr))
            if pred(msg):
                return msg
            self._pending.append(msg)

    def event(self, kind, **match):
        return self.wait(lambda m: m.get('event') == kind
                         and all(m.get(k) == v for k, v in match.items()))

    def close(self):
        if self.proc.poll() is None:
            self.proc.stdin.close()
            self.proc.wait(timeout=TIMEOUT)
        for f in (self.proc.stdout, self.proc.stderr):
            try:
                f.close()
            except Exception:
                pass


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.data = Path(self.tmp.name)
        self.h = Helper(self.data)

    def tearDown(self):
        self.h.close()
        self.tmp.cleanup()

    def account(self):
        return json.loads((self.data / 'instagram.json').read_text())

    def connect(self, username='alice'):
        r = self.h.call('login', username=username, password='s3cret-pw')
        self.assertTrue(r['ok'])
        self.h.event('login', state='done', login_id=r['result']['login_id'])

    # -- basico --------------------------------------------------------- #

    def test_ping(self):
        r = self.h.call('ping')
        self.assertEqual(r, {'id': 1, 'ok': True,
                             'result': {'pong': True, 'protocol': 1, 'fake': True}})

    def test_status_disconnected(self):
        res = self.h.call('status')['result']
        self.assertFalse(res['connected'])
        self.assertEqual(res['username'], '')
        self.assertIsNone(res['caption_template'])
        self.assertEqual(res['default_template'], '{event}\n{date}\n\n{hashtag}')
        self.assertIsNone(res['login'])
        self.assertEqual(res['max_photos'], 10)
        self.assertEqual(res['caption_max'], 2200)

    def test_unknown_and_bad_json(self):
        r = self.h.call('nope')
        self.assertFalse(r['ok'])
        self.assertEqual(r['error'], 'protocol.unknownCommand')
        self.h.send_raw(b'not json\n')
        r = self.h.wait(lambda m: m.get('id') is None and m.get('ok') is False)
        self.assertEqual(r['error'], 'protocol.badRequest')
        # Continua vivo.
        self.assertTrue(self.h.call('ping')['ok'])

    def test_eof_exits(self):
        self.h.proc.stdin.close()
        self.assertEqual(self.h.proc.wait(timeout=TIMEOUT), 0)

    def test_stray_print_does_not_hit_protocol(self):
        # Um print() perdido (ou escrita no fd 1) nao pode sujar o canal:
        # toda linha do stdout tem de ser JSON valido.
        self.connect()
        self.h.call('status')
        self.h.close()
        for line in self.h.raw:
            json.loads(line)

    # -- modelo da legenda --------------------------------------------- #

    def test_set_template(self):
        res = self.h.call('set_template', text='  {event} #x  ')['result']
        self.assertEqual(res['caption_template'], '{event} #x')
        self.assertEqual(self.account()['caption_template'], '{event} #x')
        res = self.h.call('set_template', text='')['result']
        self.assertIsNone(res['caption_template'])
        self.assertNotIn('caption_template', self.account())
        self.h.call('set_template', text='abc')
        res = self.h.call('set_template', text=None)['result']
        self.assertIsNone(res['caption_template'])

    # -- login ---------------------------------------------------------- #

    def test_login_direct(self):
        r = self.h.call('login', username='@alice', password='s3cret-pw')
        self.assertTrue(r['ok'])
        self.assertEqual(r['result']['username'], 'alice')
        lid = r['result']['login_id']
        self.h.event('login', state='running', login_id=lid)
        self.h.event('login', state='done', login_id=lid)
        st = self.h.call('status')['result']
        self.assertTrue(st['connected'])
        self.assertEqual(st['username'], 'alice')
        self.assertEqual(st['login']['state'], 'done')
        acc = self.account()
        self.assertEqual(set(acc), {'username', 'session', 'connected_at'})
        if os.name == 'posix':
            mode = stat.S_IMODE((self.data / 'instagram.json').stat().st_mode)
            self.assertEqual(mode, 0o600)

    def test_login_missing_credentials(self):
        r = self.h.call('login', username='alice', password='')
        self.assertEqual((r['ok'], r['error']), (False, 'instagram.missingCredentials'))

    def test_login_two_factor(self):
        lid = self.h.call('login', username='fake_2fa', password='pw')['result']['login_id']
        ev = self.h.event('login', state='code', login_id=lid)
        self.assertEqual(ev['kind'], 'two_factor')
        r = self.h.call('code', code=' 123 456 ')
        self.assertTrue(r['ok'])
        self.h.event('login', state='done', login_id=lid)
        self.assertTrue(self.h.call('status')['result']['connected'])

    def test_login_two_factor_bad_code(self):
        lid = self.h.call('login', username='fake_2fa', password='pw')['result']['login_id']
        self.h.event('login', state='code', login_id=lid)
        self.h.call('code', code='000000')
        ev = self.h.event('login', state='error', login_id=lid)
        self.assertEqual(ev['error'], 'instagram.badCode')

    def test_login_challenge(self):
        lid = self.h.call('login', username='fake_challenge', password='pw')['result']['login_id']
        ev = self.h.event('login', state='code', login_id=lid)
        self.assertEqual(ev['kind'], 'challenge')
        self.h.call('code', code='654321')
        self.h.event('login', state='done', login_id=lid)

    def test_login_bad_password(self):
        lid = self.h.call('login', username='fake_bad', password='pw')['result']['login_id']
        ev = self.h.event('login', state='error', login_id=lid)
        self.assertEqual(ev['error'], 'instagram.badCredentials')
        self.assertEqual(ev['detail'], '')
        self.assertFalse(self.h.call('status')['result']['connected'])

    def test_login_wait(self):
        lid = self.h.call('login', username='fake_wait', password='pw')['result']['login_id']
        self.assertEqual(self.h.event('login', state='error', login_id=lid)['error'],
                         'instagram.wait')

    def test_code_errors(self):
        self.assertEqual(self.h.call('code', code='')['error'], 'instagram.missingCode')
        self.assertEqual(self.h.call('code', code='123')['error'], 'instagram.noCodePending')

    def test_cancel_while_waiting_code(self):
        lid = self.h.call('login', username='fake_2fa', password='pw')['result']['login_id']
        self.h.event('login', state='code', login_id=lid)
        r = self.h.call('cancel')
        self.assertTrue(r['ok'])
        self.h.event('login', state='cancelled', login_id=lid)
        self.assertIsNone(self.h.call('status')['result']['login'])

    def test_cancel_while_running_discards_result(self):
        lid = self.h.call('login', username='fake_slow', password='pw')['result']['login_id']
        self.h.call('cancel')
        self.h.event('login', state='cancelled', login_id=lid)
        time.sleep(2.5)  # o login falso termina e o resultado e descartado
        self.assertFalse(self.h.call('status')['result']['connected'])
        late = [m for m in self.h._pending if m.get('event') == 'login'
                and m.get('state') == 'done']
        self.assertEqual(late, [])

    def test_new_login_supersedes_old(self):
        old = self.h.call('login', username='fake_2fa', password='pw')['result']['login_id']
        self.h.event('login', state='code', login_id=old)
        new = self.h.call('login', username='bob', password='pw')['result']['login_id']
        self.assertNotEqual(old, new)
        self.h.event('login', state='cancelled', login_id=old)
        self.h.event('login', state='done', login_id=new)
        self.assertEqual(self.h.call('status')['result']['username'], 'bob')

    def test_logout_keeps_template(self):
        self.connect()
        self.h.call('set_template', text='T')
        res = self.h.call('logout')['result']
        self.assertFalse(res['connected'])
        self.assertEqual(self.account(), {'caption_template': 'T'})

    def test_password_never_logged(self):
        self.connect()
        self.h.close()
        log_text = (self.data / 'logs' / 'instagram.log').read_text()
        self.assertIn('Instagram account connected: @alice', log_text)
        self.assertNotIn('s3cret-pw', log_text)
        self.assertNotIn('s3cret-pw', ''.join(self.h.stderr))

    # -- conta existente (formato do backend Django) ------------------- #

    def test_existing_account_file(self):
        self.h.close()
        (self.data / 'instagram.json').write_text(json.dumps({
            'username': 'legacy', 'session': {'cookies': {}, 'uuids': {}},
            'connected_at': '2026-01-01T00:00:00+00:00', 'caption_template': 'X'}))
        self.h = Helper(self.data)
        st = self.h.call('status')['result']
        self.assertEqual((st['connected'], st['username'], st['caption_template'],
                          st['connected_at']),
                         (True, 'legacy', 'X', '2026-01-01T00:00:00+00:00'))

    # -- publicar ------------------------------------------------------- #

    def photos(self, *names):
        out = []
        for n in names:
            p = self.data / n
            p.write_bytes(b'\xff\xd8\xff\xd9')
            out.append(str(p))
        return out

    def test_publish_not_connected(self):
        r = self.h.call('publish', files=self.photos('a.jpg'), caption='hi')
        self.assertEqual(r['error'], 'instagram.notConnected')

    def test_publish_validation(self):
        self.connect()
        self.assertEqual(self.h.call('publish', files=[], caption='')['error'],
                         'instagram.noPhotos')
        many = self.photos(*[f'{i}.jpg' for i in range(11)])
        r = self.h.call('publish', files=many, caption='')
        self.assertEqual((r['error'], r['params']), ('instagram.tooMany', {'max': 10}))
        r = self.h.call('publish', files=many[:1], caption='x' * 2201)
        self.assertEqual((r['error'], r['params']), ('instagram.captionTooLong', {'max': 2200}))
        r = self.h.call('publish', files=[str(self.data / 'missing.jpg')], caption='')
        self.assertEqual(r['error'], 'instagram.publishFailed')

    def test_publish_single(self):
        self.connect()
        r = self.h.call('publish', files=self.photos('a.jpg'), caption='hello')
        self.assertTrue(r['ok'])
        pid = r['result']['publish_id']
        self.assertEqual(r['result']['total'], 1)
        ev = self.h.event('publish', state='running', publish_id=pid)
        self.assertEqual(ev['phase'], 'upload')
        ev = self.h.event('publish', state='done', publish_id=pid)
        self.assertEqual(ev['code'], 'FAKE1')
        self.assertEqual(ev['url'], 'https://www.instagram.com/p/FAKE1/')
        self.assertEqual(ev['username'], 'alice')
        self.assertTrue(ev['media_id'])

    def test_publish_album(self):
        self.connect()
        r = self.h.call('publish', files=self.photos('a.jpg', 'b.jpg', 'c.jpg'), caption='')
        ev = self.h.event('publish', state='done', publish_id=r['result']['publish_id'])
        self.assertEqual(ev['code'], 'FAKE3')

    def test_publish_failure(self):
        self.connect()
        r = self.h.call('publish', files=self.photos('fail.jpg'), caption='')
        ev = self.h.event('publish', state='error', publish_id=r['result']['publish_id'])
        self.assertEqual(ev['error'], 'instagram.publishFailed')
        self.assertIn('fake upload failure', ev['detail'])
        # Depois do erro, pode publicar de novo.
        r = self.h.call('publish', files=self.photos('ok.jpg'), caption='')
        self.assertTrue(r['ok'])
        self.h.event('publish', state='done', publish_id=r['result']['publish_id'])


if __name__ == '__main__':
    unittest.main()

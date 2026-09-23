"""Exportar: todas as fotos ativas, com os ajustes, em ``<project>/publicar``.

Roda numa thread em segundo plano (pode levar minutos) e expoe o progresso
em ``status()`` — o frontend consulta enquanto mostra o modal (TODO 7.3).

Reexportar so regrava o que mudou (TODO 7.4): cada foto guarda o
``exported_hash`` do que foi escrito (arquivo original + ajustes + versao do
motor e do formato). Igual e com o arquivo no lugar = pula. Foto excluida
depois de exportada tem a copia EXPORTADA removida de publicar/ — so os
arquivos que o proprio editor escreveu, nunca um original.

O estado do job vive na memoria do processo: o uwsgi roda um processo so
(com threads), entao todas as requisicoes enxergam o mesmo job.
"""
import hashlib
import logging
import threading
from concurrent.futures import ThreadPoolExecutor

from django.conf import settings
from django.db import connection
from django.utils import timezone

from photoeditor.imaging import develop, publish
from photoeditor.imaging.io import raw_path
from photoeditor.models import Photo
from photoeditor.services import editing
from photoeditor.services.derivatives import write_atomic

log = logging.getLogger(__name__)

# Sobe quando o formato de saida muda (caixa, qualidade, EXIF...).
EXPORT_VERSION = 1
# Duas fotos por vez: numpy e o codec JPEG soltam o GIL, e cada render de
# 1080p segura ~150 MB — mais workers so disputariam memoria.
WORKERS = 2

_lock = threading.Lock()
_state = {'running': False, 'total': 0, 'done': 0, 'written': 0, 'skipped': 0,
          'removed': 0, 'errors': [], 'started_at': None, 'finished_at': None}


def _snapshot() -> dict:
    """Copia do estado; quem chama segura o ``_lock``."""
    return {**_state, 'errors': list(_state['errors']),
            'output_dir': settings.PUBLISH_DIR.name}


def status() -> dict:
    with _lock:
        return _snapshot()


def export_hash(photo, state) -> str:
    key = (f'{EXPORT_VERSION}:{develop.settings_hash(state["values"], state["preset"])}'
           f':{photo.mtime_ns}:{photo.size_bytes}')
    return hashlib.sha1(key.encode()).hexdigest()[:16]


def start() -> dict:
    """Dispara a exportacao; se ja houver uma rodando, so devolve o estado."""
    with _lock:
        if _state['running']:
            return _snapshot()
        _state.update(running=True, total=0, done=0, written=0, skipped=0,
                      removed=0, errors=[], started_at=timezone.now().isoformat(),
                      finished_at=None)
        snapshot = _snapshot()
    threading.Thread(target=_run, name='export', daemon=True).start()
    return snapshot


def _bump(**inc):
    with _lock:
        for k, v in inc.items():
            _state[k] += v


def _export_one(photo):
    try:
        state = editing.get_state(photo)
        digest = export_hash(photo, state)
        out = settings.PUBLISH_DIR / photo.file_name
        if photo.exported_hash == digest and out.is_file():
            _bump(skipped=1)
            return
        rgb, exif = publish.load(raw_path(photo))
        rendered = develop.render(rgb, state['values'], state['preset'])
        write_atomic(out, publish.encode(rendered, exif))
        Photo.objects.filter(pk=photo.pk).update(exported_hash=digest)
        _bump(written=1)
    except Exception:
        log.exception("Export failed for %s", photo.file_name)
        with _lock:
            _state['errors'].append(photo.file_name)
    finally:
        _bump(done=1)
        connection.close()      # cada thread do pool abre a sua conexao


def _remove_stale():
    """Tira de publicar/ o que o editor exportou de fotos que nao estao mais
    ativas (excluidas ou sumidas)."""
    stale = Photo.objects.exclude(status=Photo.Status.ACTIVE).exclude(exported_hash='')
    for photo in stale:
        (settings.PUBLISH_DIR / photo.file_name).unlink(missing_ok=True)
        Photo.objects.filter(pk=photo.pk).update(exported_hash='')
        _bump(removed=1)


def _run():
    try:
        settings.PUBLISH_DIR.mkdir(parents=True, exist_ok=True)
        photos = list(Photo.objects.filter(status=Photo.Status.ACTIVE)
                      .select_related('adjustment'))
        with _lock:
            _state['total'] = len(photos)
        log.info("Export started: %d photos -> %s", len(photos), settings.PUBLISH_DIR)
        with ThreadPoolExecutor(max_workers=WORKERS) as pool:
            list(pool.map(_export_one, photos))
        _remove_stale()
    except Exception:
        log.exception("Export aborted")
        with _lock:
            _state['errors'].append('*')
    finally:
        with _lock:
            _state['running'] = False
            _state['finished_at'] = timezone.now().isoformat()
            log.info("Export finished: %s", {k: _state[k] for k in
                                             ('total', 'written', 'skipped', 'removed')})
        connection.close()

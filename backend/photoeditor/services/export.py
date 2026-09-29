"""Exportar: todas as fotos ativas, com os ajustes, em ``<project>/publicar``.

Roda numa thread em segundo plano (pode levar minutos) e expoe o progresso
em ``status()`` — o frontend consulta enquanto mostra o modal (TODO 7.3).

Reexportar so regrava o que mudou (TODO 7.4): cada foto guarda o
``exported_hash`` do que foi escrito (arquivo original + ajustes + versao do
motor e do formato). Igual e com o arquivo no lugar = pula. Foto excluida
depois de exportada tem a copia EXPORTADA removida de publicar/ — so os
arquivos que o proprio editor escreveu, nunca um original.

Base de merge sai como ``<base>_merge.jpg`` (o merge composto, com os
ajustes da base) e as fotos que sao camada de um merge nao saem.

A capa do evento (``services/cover.py``) sai tambem como ``capa.jpg``: copia
exata do que foi exportado para aquela foto. Sem capa (ou com a capa fora da
filmstrip), um ``capa.jpg`` antigo sai de publicar/.

O estado do job vive na memoria do processo: o servidor local (waitress,
desktop/server.py) e um processo so com threads, entao todas as requisicoes
enxergam o mesmo job.
"""
import hashlib
import logging
import threading
from concurrent.futures import ThreadPoolExecutor

from django.conf import settings
from django.db import connection
from django.db.models import Q
from django.utils import timezone

from photoeditor.imaging import develop, publish
from photoeditor.imaging.io import raw_path
from photoeditor.models import Photo
from photoeditor.services import catalog, cover, editing, layers, merges
from photoeditor.services.derivatives import write_atomic

log = logging.getLogger(__name__)

# Sobe quando o formato de saida muda (caixa, qualidade, EXIF...).
EXPORT_VERSION = 2      # 2: EXIF Software com nome, versao e URL do editor
# Nome da capa em publicar/ (pedido do fluxo de publicacao, por isso em PT).
COVER_FILE_NAME = 'capa.jpg'
# Duas fotos por vez: numpy e o codec JPEG soltam o GIL, e cada render de
# 1080p segura ~150 MB — mais workers so disputariam memoria.
WORKERS = 2

_lock = threading.Lock()
_state = {'running': False, 'total': 0, 'done': 0, 'written': 0, 'skipped': 0,
          'removed': 0, 'errors': [], 'started_at': None, 'finished_at': None,
          'cover': None}


def _snapshot() -> dict:
    """Copia do estado; quem chama segura o ``_lock``."""
    return {**_state, 'errors': list(_state['errors']),
            'output_dir': settings.PUBLISH_DIR.name}


def status() -> dict:
    with _lock:
        return _snapshot()


def export_hash(photo, state, merge_version='') -> str:
    edit = develop.settings_hash(state['values'], state['preset'], state['crop'],
                                 state['layers'])
    key = (f'{EXPORT_VERSION}:{edit}'
           f':{photo.mtime_ns}:{photo.size_bytes}:{merge_version}')
    return hashlib.sha1(key.encode()).hexdigest()[:16]


def start() -> dict:
    """Dispara a exportacao; se ja houver uma rodando, so devolve o estado."""
    with _lock:
        if _state['running']:
            return _snapshot()
        _state.update(running=True, total=0, done=0, written=0, skipped=0,
                      removed=0, errors=[], started_at=timezone.now().isoformat(),
                      finished_at=None, cover=None)
        snapshot = _snapshot()
    threading.Thread(target=_run, name='export', daemon=True).start()
    return snapshot


def _bump(**inc):
    with _lock:
        for k, v in inc.items():
            _state[k] += v


def _unlink_output(name):
    """Tira de publicar/ um arquivo que o editor escreveu — a menos que ele
    seja o nome de uma foto ativa do catalogo (entao e a exportacao dela)."""
    if not Photo.objects.filter(file_name=name, status=Photo.Status.ACTIVE).exists():
        (settings.PUBLISH_DIR / name).unlink(missing_ok=True)


def _export_one(photo, merge=None):
    try:
        state = editing.get_state(photo)
        merge_version = merges.version(merge)
        digest = export_hash(photo, state, merge_version)
        name = merges.export_name(photo) if merge_version else photo.file_name
        out = settings.PUBLISH_DIR / name
        if photo.exported_hash == digest and out.is_file():
            _bump(skipped=1)
            return
        # Recorta na resolucao cheia, depois reduz para a caixa, depois revela
        # (as mascaras das camadas passam pelo mesmo crop e pela mesma reducao).
        if merge_version:
            rgb, exif = merges.render_full(merge)
        else:
            rgb, exif = publish.load(raw_path(photo), state['crop']['scale'])
        rendered = layers.develop_image(rgb, state, fit=publish.fit)
        write_atomic(out, publish.encode(rendered, exif, publish.software_tag()))
        # Virou (ou deixou de ser) merge: a saida com o outro nome e velha.
        if merge_version:
            (settings.PUBLISH_DIR / photo.file_name).unlink(missing_ok=True)
        else:
            _unlink_output(merges.export_name(photo))
        Photo.objects.filter(pk=photo.pk).update(exported_hash=digest)
        _bump(written=1)
    except Exception:
        log.exception("Export failed for %s", photo.file_name)
        with _lock:
            _state['errors'].append(photo.file_name)
    finally:
        _bump(done=1)
        connection.close()      # cada thread do pool abre a sua conexao


def _remove_stale(hidden):
    """Tira de publicar/ o que o editor exportou de fotos que sairam da
    filmstrip: excluidas, sumidas, copias sem a original ou camadas de um
    merge."""
    stale = (Photo.objects.exclude(exported_hash='')
             .exclude(Q(pk__in=catalog.visible()) & ~Q(pk__in=hidden)))
    for photo in stale:
        (settings.PUBLISH_DIR / photo.file_name).unlink(missing_ok=True)
        _unlink_output(merges.export_name(photo))
        Photo.objects.filter(pk=photo.pk).update(exported_hash='')
        _bump(removed=1)


def _export_cover(visible_ids, index):
    """``capa.jpg`` = copia do que a exportacao escreveu para a capa.

    So regrava quando o conteudo mudou (o arquivo da foto ja passou pelo
    pulo de ``exported_hash``). Capa ausente, excluida ou escondida num merge:
    o ``capa.jpg`` antigo sai. Uma foto do catalogo chamada ``capa.jpg`` tem a
    preferencia pelo nome — ai a capa nao e gravada (e o log avisa).
    """
    target = settings.PUBLISH_DIR / COVER_FILE_NAME
    if Photo.objects.filter(file_name__iexact=COVER_FILE_NAME,
                            status=Photo.Status.ACTIVE).exists():
        log.warning("A photo is named %s: the cover file is not written.", COVER_FILE_NAME)
        return
    photo = cover.current()
    if photo is None or photo.pk not in visible_ids:
        target.unlink(missing_ok=True)
        return
    name = merges.export_name(photo) if merges.version(index.get(photo.pk)) else photo.file_name
    source = settings.PUBLISH_DIR / name
    if not source.is_file():
        # A exportacao da propria foto falhou (ja esta nos erros).
        return
    data = source.read_bytes()
    if not (target.is_file() and target.read_bytes() == data):
        write_atomic(target, data)
    with _lock:
        _state['cover'] = COVER_FILE_NAME
    log.info("Cover exported: %s -> %s", name, COVER_FILE_NAME)


def _run():
    try:
        settings.PUBLISH_DIR.mkdir(parents=True, exist_ok=True)
        hidden = merges.hidden_ids()
        index = merges.index()
        photos = list(catalog.visible().exclude(pk__in=hidden)
                      .select_related('adjustment', 'copy_of'))
        with _lock:
            _state['total'] = len(photos)
        log.info("Export started: %d photos -> %s", len(photos), settings.PUBLISH_DIR)
        with ThreadPoolExecutor(max_workers=WORKERS) as pool:
            list(pool.map(lambda p: _export_one(p, index.get(p.pk)), photos))
        _remove_stale(hidden)
        _export_cover({p.pk for p in photos}, index)
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

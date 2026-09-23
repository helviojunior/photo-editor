"""Derivados em cache (preview e thumbnail) em ``<project>/project_data/cache``.

Gerados sob demanda a partir do original, que nunca e alterado. O nome do
arquivo carrega o ``mtime`` do original: trocar a foto invalida o cache sem
precisar apagar nada.
"""
import os
import tempfile
from pathlib import Path

from django.conf import settings

import numpy as np
from PIL import Image

from photoeditor.imaging import develop
from photoeditor.imaging.io import load_rgb, raw_path

# Lado maior do preview que o editor mostra (original e editada lado a lado).
PREVIEW_SIDE = 1600
THUMBNAIL_SIDE = 240
PREVIEW_QUALITY = 92
RENDER_QUALITY = 90
THUMBNAIL_QUALITY = 82


def cache_dir(kind: str) -> Path:
    path = settings.PROJECT_DATA_DIR / 'cache' / kind
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_atomic(path: Path, data: bytes):
    """Grava em arquivo temporario e renomeia: requisicoes simultaneas nunca
    leem um JPEG pela metade."""
    fd, tmp = tempfile.mkstemp(dir=path.parent, suffix='.tmp')
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def _encode(img, quality: int) -> bytes:
    import io
    buf = io.BytesIO()
    img.save(buf, format='JPEG', quality=quality, optimize=True)
    return buf.getvalue()


def preview_path(photo) -> Path:
    """Preview do ORIGINAL (sem edicao), na orientacao de exibicao."""
    path = cache_dir('preview') / f'{photo.pk}-{photo.mtime_ns}.jpg'
    if not path.is_file():
        img = load_rgb(raw_path(photo), max_side=PREVIEW_SIDE)
        write_atomic(path, _encode(img, PREVIEW_QUALITY))
    return path


def thumbnail_path(photo) -> Path:
    """Thumbnail do original para a filmstrip."""
    path = cache_dir('thumbnail') / f'{photo.pk}-{photo.mtime_ns}.jpg'
    if not path.is_file():
        img = load_rgb(preview_path(photo), max_side=THUMBNAIL_SIDE)
        write_atomic(path, _encode(img, THUMBNAIL_QUALITY))
    return path


def render_path(photo, values, preset='') -> Path:
    """Preview EDITADO: o preview do original passado pelo motor de revelacao.

    O nome carrega o hash dos ajustes efetivos (e a versao do motor), entao
    arrastar um slider de volta a um valor ja visto reaproveita o arquivo.
    """
    if develop.is_neutral(values, preset):
        return preview_path(photo)
    key = develop.settings_hash(values, preset)
    path = cache_dir('render') / f'{photo.pk}-{photo.mtime_ns}-{key}.jpg'
    if not path.is_file():
        with Image.open(preview_path(photo)) as img:
            rgb = np.asarray(img.convert('RGB'))
        out = Image.fromarray(develop.render(rgb, values, preset))
        write_atomic(path, _encode(out, RENDER_QUALITY))
    return path

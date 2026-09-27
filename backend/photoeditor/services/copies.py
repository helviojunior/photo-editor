"""Duplicar: copia virtual de uma foto.

A copia e uma entrada nova no catalogo que le o MESMO JPEG da original (nada
e gravado em raw/) e tem ajustes, crop e camadas proprios — nasce com os da
foto duplicada. Na filmstrip fica ao lado da original (mesma data de captura);
o Exportar a grava como ``<nome>_copy.jpg``.

Copia de copia aponta para a original de verdade, que e de quem o arquivo e.
Desfazer (CTRL/CMD+Z) tira a copia do catalogo.
"""
import logging
from pathlib import Path

from django.conf import settings
from django.db import transaction

from photoeditor.models import HistoryEntry, Photo
from photoeditor.services import editing, history

log = logging.getLogger(__name__)

COPY_SUFFIX = '_copy'
# Campos lidos do arquivo: a copia tem os mesmos da original.
FILE_FIELDS = ('size_bytes', 'mtime_ns', 'width', 'height', 'orientation', 'captured_at')


def copy_name(root) -> str:
    """``<nome>_copy.<ext>``, depois ``_copy2``, ``_copy3``... — livre no
    catalogo (inclusive entre excluidas) e em raw/."""
    name = Path(root.file_name)
    n = 1
    while True:
        candidate = f'{name.stem}{COPY_SUFFIX}{"" if n == 1 else n}{name.suffix}'
        if not Photo.objects.filter(file_name=candidate).exists() \
                and not (settings.RAW_DIR / candidate).exists():
            return candidate
        n += 1


def duplicate(photo) -> Photo:
    root = photo.copy_of or photo
    with transaction.atomic():
        copy = Photo.objects.create(
            file_name=copy_name(root), copy_of=root, status=Photo.Status.ACTIVE,
            **{f: getattr(root, f) for f in FILE_FIELDS})
        editing.copy_state(photo, copy)
        history.record(copy, HistoryEntry.Kind.DUPLICATE,
                       before={'status': Photo.Status.DELETED},
                       after={'status': Photo.Status.ACTIVE, 'copy_of': str(root.pk)})
    log.info("Duplicated %s -> %s", photo.file_name, copy.file_name)
    return copy


def _undo_duplicate(entry):
    photo = entry.photo
    photo.status = Photo.Status.DELETED
    photo.save(update_fields=['status', 'updated'])


history.register_undo(HistoryEntry.Kind.DUPLICATE, _undo_duplicate)

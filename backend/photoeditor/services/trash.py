"""Exclusao de fotos: o arquivo e MOVIDO de raw/ para deleted/, nunca apagado.

Desfazer devolve o arquivo para raw/ com o nome original. O ``mtime`` passa
intacto pelo ``rename`` (mesmo volume), entao os derivados em cache continuam
validos.
"""
import logging
import os

from django.conf import settings
from django.db import transaction

from photoeditor.imaging.io import raw_path
from photoeditor.models import HistoryEntry, Photo
from photoeditor.services import history

log = logging.getLogger(__name__)


def _free_name(directory, file_name):
    """``file_name`` ou, se ja existir em ``directory``, ``nome-1.jpg``, ..."""
    stem, dot, ext = file_name.rpartition('.')
    if not dot:
        stem, ext = file_name, ''
    candidate, n = file_name, 0
    while (directory / candidate).exists():
        n += 1
        candidate = f'{stem}-{n}.{ext}' if ext else f'{stem}-{n}'
    return candidate


def delete_photo(photo):
    source = raw_path(photo)
    if not source.is_file():
        raise history.ActionError('error.photoFileMissing', name=photo.file_name)

    settings.DELETED_DIR.mkdir(parents=True, exist_ok=True)
    target_name = _free_name(settings.DELETED_DIR, photo.file_name)
    with transaction.atomic():
        os.replace(source, settings.DELETED_DIR / target_name)
        photo.status = Photo.Status.DELETED
        photo.deleted_file_name = target_name
        photo.save(update_fields=['status', 'deleted_file_name', 'updated'])
        history.record(photo, HistoryEntry.Kind.DELETE,
                       before={'status': Photo.Status.ACTIVE},
                       after={'status': Photo.Status.DELETED,
                              'deleted_file_name': target_name})
    log.info("Deleted %s -> deleted/%s", photo.file_name, target_name)


def _undo_delete(entry):
    photo = entry.photo
    name = entry.after.get('deleted_file_name') or photo.deleted_file_name \
        or photo.file_name
    source = settings.DELETED_DIR / name
    target = raw_path(photo)
    if target.exists():
        raise history.ActionError('error.restoreConflict', name=photo.file_name)
    if not source.is_file():
        raise history.ActionError('error.photoFileMissing', name=name)
    os.replace(source, target)
    photo.status = Photo.Status.ACTIVE
    photo.deleted_file_name = ''
    photo.save(update_fields=['status', 'deleted_file_name', 'updated'])


history.register_undo(HistoryEntry.Kind.DELETE, _undo_delete)

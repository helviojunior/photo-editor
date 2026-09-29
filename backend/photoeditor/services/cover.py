"""Capa do evento: uma foto do projeto que o Exportar grava tambem como
``publicar/capa.jpg`` (ver ``services/export.py``).

No maximo uma por projeto — o banco garante (``photo_single_cover``). Marcar
outra foto tira a marca da anterior; desmarcar deixa o projeto sem capa.
Desfazivel (CTRL/CMD+Z): o historico guarda qual foto era a capa antes.
"""
import logging

from django.db import transaction

from photoeditor.models import HistoryEntry, Photo
from photoeditor.services import history

log = logging.getLogger(__name__)


def current():
    """A foto marcada como capa (qualquer status), ou None."""
    return Photo.objects.filter(is_cover=True).first()


def _apply(photo_id):
    """Deixa ``photo_id`` como a unica capa (None = sem capa)."""
    Photo.objects.filter(is_cover=True).exclude(pk=photo_id).update(is_cover=False)
    if photo_id:
        Photo.objects.filter(pk=photo_id).update(is_cover=True)


def set_cover(photo, on=True):
    """Marca (``on``) ou desmarca ``photo`` como capa. Sem mudanca, nao grava."""
    with transaction.atomic():
        before = current()
        before_id = before.pk if before else None
        after_id = photo.pk if on else (None if before_id == photo.pk else before_id)
        if after_id == before_id:
            return photo
        _apply(after_id)
        history.record(photo, HistoryEntry.Kind.COVER,
                       before={'cover': str(before_id) if before_id else None},
                       after={'cover': str(after_id) if after_id else None})
    log.info("Cover %s: %s", 'set' if on else 'cleared', photo.file_name)
    photo.refresh_from_db(fields=['is_cover'])
    return photo


def _undo_cover(entry):
    _apply(entry.before.get('cover'))


history.register_undo(HistoryEntry.Kind.COVER, _undo_cover)

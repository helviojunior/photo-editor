"""Historico de acoes por foto e o desfazer (CTRL/CMD+Z).

Cada acao que muda uma foto grava um ``HistoryEntry`` com o estado de antes e
o de depois. Desfazer pega a ultima acao ainda valida, devolve a foto ao
``before`` e marca ``undone_at`` — o registro nunca e apagado (TODO 5.7).

Quem sabe desfazer cada tipo de acao se registra aqui (``register_undo``):
exclusao em ``trash``, ajustes em ``editing``.
"""
import logging
import threading

from django.db import transaction
from django.utils import timezone

from photoeditor.models import HistoryEntry

log = logging.getLogger(__name__)

_UNDO = {}
# Dois CTRL+Z seguidos nao podem desfazer a mesma acao duas vezes.
_lock = threading.Lock()


class ActionError(Exception):
    """A acao nao pode ser feita ou desfeita agora; ``key`` e a chave de i18n."""

    def __init__(self, key, **params):
        super().__init__(key)
        self.key = key
        self.params = params


def register_undo(kind, fn):
    """``fn(entry)`` devolve a foto ao ``entry.before``."""
    _UNDO[kind] = fn


def record(photo, kind, before, after):
    return HistoryEntry.objects.create(photo=photo, kind=kind,
                                       before=before, after=after)


def undo_last():
    """Desfaz a ultima acao ainda nao desfeita. ``None`` se nao ha nenhuma."""
    with _lock, transaction.atomic():
        entry = (HistoryEntry.objects.select_related('photo')
                 .filter(undone_at__isnull=True).order_by('-created').first())
        if entry is None:
            return None
        _UNDO[entry.kind](entry)
        entry.undone_at = timezone.now()
        entry.save(update_fields=['undone_at', 'updated'])
    log.info("Undone %s on %s", entry.kind, entry.photo.file_name)
    return entry


def entry_json(entry):
    return {
        'id': str(entry.pk),
        'kind': entry.kind,
        'photo_id': str(entry.photo_id),
        'created': entry.created.isoformat(),
        'undone_at': entry.undone_at.isoformat() if entry.undone_at else None,
    }

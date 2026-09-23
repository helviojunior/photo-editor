"""Ajustes das fotos: ler, gravar (com historico), Auto, preset e reset.

Toda mudanca passa por ``apply_state``, que grava o ``HistoryEntry`` com o
estado de antes e o de depois; desfazer volta ao de antes sem gerar entrada
nova (o registro desfeito fica, marcado — TODO 5.7).
"""
import logging

import numpy as np
from PIL import Image

from photoeditor.imaging import auto, develop
from photoeditor.models import Adjustment, HistoryEntry
from photoeditor.services import derivatives, history

log = logging.getLogger(__name__)

SLIDER_FIELDS = tuple(develop.SLIDERS)


def get_state(photo) -> dict:
    """``{'values': {...}, 'preset': ''}`` — neutro se a foto nunca foi editada."""
    try:
        adj = photo.adjustment
    except Adjustment.DoesNotExist:
        return {'values': dict(develop.NEUTRAL), 'preset': ''}
    return {
        'values': develop.normalize({f: getattr(adj, f) for f in SLIDER_FIELDS}),
        'preset': develop.normalize_preset(adj.preset),
    }


def _normalize_state(values, preset):
    return {'values': develop.normalize(values),
            'preset': develop.normalize_preset(preset)}


def _save_state(photo, state):
    adj, _ = Adjustment.objects.get_or_create(photo=photo)
    for field, value in state['values'].items():
        setattr(adj, field, value)
    adj.preset = state['preset']
    adj.save()
    photo.adjustment = adj


def apply_state(photo, values, preset, kind):
    """Grava o novo estado e o historico. Sem mudanca real, nao grava nada."""
    before = get_state(photo)
    after = _normalize_state(values, preset)
    if after == before:
        return after
    _save_state(photo, after)
    history.record(photo, kind, before=before, after=after)
    return after


def set_adjustments(photo, values, preset):
    before = get_state(photo)
    kind = HistoryEntry.Kind.ADJUST
    if develop.normalize(values) == before['values'] \
            and develop.normalize_preset(preset) != before['preset']:
        kind = HistoryEntry.Kind.PRESET
    return apply_state(photo, values, preset, kind)


def run_auto(photo):
    """Auto sobre o preview: a analise roda numa miniatura de 750 px de
    qualquer forma, e o preview ja esta em cache e na orientacao certa."""
    with Image.open(derivatives.preview_path(photo)) as img:
        rgb = np.asarray(img.convert('RGB'))
    values, case = auto.auto_values(rgb)
    state = get_state(photo)
    log.info("Auto on %s: case=%s %s", photo.file_name, case, values)
    # Luz e branco saem do Auto; vibrance, saturation e preset ficam.
    return apply_state(photo, {**state['values'], **values}, state['preset'],
                       HistoryEntry.Kind.AUTO)


def reset(photo):
    return apply_state(photo, develop.NEUTRAL, '', HistoryEntry.Kind.RESET)


def _undo_state(entry):
    before = entry.before or {}
    _save_state(entry.photo, _normalize_state(before.get('values'), before.get('preset', '')))


for _kind in (HistoryEntry.Kind.ADJUST, HistoryEntry.Kind.AUTO,
              HistoryEntry.Kind.PRESET, HistoryEntry.Kind.RESET):
    history.register_undo(_kind, _undo_state)

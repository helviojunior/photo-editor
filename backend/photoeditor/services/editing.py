"""Ajustes das fotos: ler, gravar (com historico), Auto, preset, crop e reset.

O estado de uma foto e ``{'values': {...}, 'preset': '', 'crop': {...}}``.
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
CROP_FIELDS = {'scale': 'crop_scale', 'cx': 'crop_cx', 'cy': 'crop_cy',
               'angle': 'crop_angle'}


def aspect(photo) -> float:
    """Altura/largura na orientacao de exibicao — o que o crop preserva."""
    return photo.height / photo.width if photo.width and photo.height else 1.0


def get_state(photo) -> dict:
    """Estado gravado da foto — neutro se ela nunca foi editada."""
    try:
        adj = photo.adjustment
    except Adjustment.DoesNotExist:
        return normalize_state(photo, None, '', None)
    return normalize_state(
        photo,
        {f: getattr(adj, f) for f in SLIDER_FIELDS},
        adj.preset,
        {k: getattr(adj, f) for k, f in CROP_FIELDS.items()},
    )


def normalize_state(photo, values, preset, crop):
    return {'values': develop.normalize(values),
            'preset': develop.normalize_preset(preset),
            'crop': develop.normalize_crop(crop, aspect(photo))}


def _save_state(photo, state):
    adj, _ = Adjustment.objects.get_or_create(photo=photo)
    for field, value in state['values'].items():
        setattr(adj, field, value)
    adj.preset = state['preset']
    for key, field in CROP_FIELDS.items():
        setattr(adj, field, state['crop'][key])
    adj.save()
    photo.adjustment = adj


def apply_state(photo, values, preset, crop, kind):
    """Grava o novo estado e o historico. Sem mudanca real, nao grava nada."""
    before = get_state(photo)
    after = normalize_state(photo, values, preset, crop)
    if after == before:
        return after
    _save_state(photo, after)
    history.record(photo, kind, before=before, after=after)
    return after


def set_adjustments(photo, values, preset, crop=None):
    """Grava o que o painel mandou. ``crop`` ausente = mantem o gravado.

    O tipo da acao no historico e o do que mudou SOZINHO (so o preset, so o
    crop); qualquer outra combinacao e um ajuste."""
    before = get_state(photo)
    after = normalize_state(photo, values, preset,
                            before['crop'] if crop is None else crop)
    changed = {k for k in after if after[k] != before[k]}
    kind = {frozenset({'preset'}): HistoryEntry.Kind.PRESET,
            frozenset({'crop'}): HistoryEntry.Kind.CROP}.get(
                frozenset(changed), HistoryEntry.Kind.ADJUST)
    return apply_state(photo, after['values'], after['preset'], after['crop'], kind)


def run_auto(photo):
    """Auto sobre o preview JA RECORTADO: a exposicao e medida no que fica na
    foto. A analise roda numa miniatura de 750 px de qualquer forma, e o
    preview ja esta em cache e na orientacao certa."""
    state = get_state(photo)
    with Image.open(derivatives.preview_path(photo)) as img:
        rgb = np.asarray(img.convert('RGB'))
    values, case = auto.auto_values(develop.apply_crop(rgb, state['crop']))
    log.info("Auto on %s: case=%s %s", photo.file_name, case, values)
    # Luz e branco saem do Auto; vibrance, saturation, preset e crop ficam.
    return apply_state(photo, {**state['values'], **values}, state['preset'],
                       state['crop'], HistoryEntry.Kind.AUTO)


def reset(photo):
    """Volta ao neutro — ajustes, preset e crop."""
    return apply_state(photo, develop.NEUTRAL, '', None, HistoryEntry.Kind.RESET)


def _undo_state(entry):
    # Entradas de antes do crop nao tem a chave: normalizar da o quadro inteiro.
    before = entry.before or {}
    _save_state(entry.photo, normalize_state(
        entry.photo, before.get('values'), before.get('preset', ''), before.get('crop')))


for _kind in (HistoryEntry.Kind.ADJUST, HistoryEntry.Kind.AUTO,
              HistoryEntry.Kind.PRESET, HistoryEntry.Kind.RESET,
              HistoryEntry.Kind.CROP):
    history.register_undo(_kind, _undo_state)

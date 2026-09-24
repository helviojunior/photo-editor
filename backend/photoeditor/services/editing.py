"""Ajustes das fotos: ler, gravar (com historico), Auto, preset, crop, camadas
e reset.

O estado de uma foto e ``{'values': {...}, 'preset': '', 'crop': {...},
'layers': [...]}``; com camadas, ``values``/``preset`` valem para o restante
da foto (ver ``develop.normalize_layers``).
Toda mudanca passa por ``apply_state``, que grava o ``HistoryEntry`` com o
estado de antes e o de depois; desfazer volta ao de antes sem gerar entrada
nova (o registro desfeito fica, marcado — TODO 5.7).
"""
import logging

import numpy as np
from PIL import Image

from photoeditor.imaging import auto, develop
from photoeditor.models import Adjustment, HistoryEntry
from photoeditor.services import derivatives, history, layers

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
        return normalize_state(photo, None, '', None, None)
    return normalize_state(
        photo,
        {f: getattr(adj, f) for f in SLIDER_FIELDS},
        adj.preset,
        {k: getattr(adj, f) for k, f in CROP_FIELDS.items()},
        adj.layers,
    )


def normalize_state(photo, values, preset, crop, layers):
    return {'values': develop.normalize(values),
            'preset': develop.normalize_preset(preset),
            'crop': develop.normalize_crop(crop, aspect(photo)),
            'layers': develop.normalize_layers(layers)}


def _save_state(photo, state):
    adj, _ = Adjustment.objects.get_or_create(photo=photo)
    for field, value in state['values'].items():
        setattr(adj, field, value)
    adj.preset = state['preset']
    for key, field in CROP_FIELDS.items():
        setattr(adj, field, state['crop'][key])
    adj.layers = state['layers']
    adj.save()
    photo.adjustment = adj


def apply_state(photo, values, preset, crop, layers, kind):
    """Grava o novo estado e o historico. Sem mudanca real, nao grava nada."""
    before = get_state(photo)
    after = normalize_state(photo, values, preset, crop, layers)
    if after == before:
        return after
    _save_state(photo, after)
    history.record(photo, kind, before=before, after=after)
    return after


def set_adjustments(photo, values, preset, crop=None, layers=None):
    """Grava o que o painel mandou. ``crop``/``layers`` ausentes = mantem o
    gravado.

    O tipo da acao no historico e o do que mudou SOZINHO (so o preset, so o
    crop, so as camadas); qualquer outra combinacao e um ajuste."""
    before = get_state(photo)
    after = normalize_state(photo, values, preset,
                            before['crop'] if crop is None else crop,
                            before['layers'] if layers is None else layers)
    changed = {k for k in after if after[k] != before[k]}
    kind = {frozenset({'preset'}): HistoryEntry.Kind.PRESET,
            frozenset({'crop'}): HistoryEntry.Kind.CROP,
            frozenset({'layers'}): HistoryEntry.Kind.LAYER}.get(
                frozenset(changed), HistoryEntry.Kind.ADJUST)
    return apply_state(photo, after['values'], after['preset'], after['crop'],
                       after['layers'], kind)


def _bbox(mask):
    """Caixa da area de uma mascara (uint8), ou ``None`` se vazia."""
    ys, xs = np.nonzero(mask > 127)
    if not len(xs):
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def run_auto(photo, layer_id=None):
    """Auto sobre o preview JA RECORTADO: a exposicao e medida no que fica na
    foto. A analise roda numa miniatura de 750 px de qualquer forma, e o
    preview ja esta em cache e na orientacao certa.

    Com ``layer_id``, mede so a caixa da area da camada (na foto inteira, onde
    a mascara vive) e grava nos ajustes DELA."""
    state = get_state(photo)
    with Image.open(derivatives.preview_path(photo)) as img:
        rgb = np.asarray(img.convert('RGB'))
    layer = next((lay for lay in state['layers'] if lay['id'] == layer_id), None)
    if layer is not None:
        mask = layers.load_mask(layer['mask'])
        box = _bbox(mask) if mask is not None and mask.shape == rgb.shape[:2] else None
        if box is None:
            return state
        x0, y0, x1, y1 = box
        values, case = auto.auto_values(np.ascontiguousarray(rgb[y0:y1, x0:x1]))
        log.info("Auto on %s layer %s: case=%s %s", photo.file_name, layer_id, case, values)
        new_layers = [{**lay, 'values': {**lay['values'], **values}} if lay is layer else lay
                      for lay in state['layers']]
        return apply_state(photo, state['values'], state['preset'], state['crop'],
                           new_layers, HistoryEntry.Kind.AUTO)

    values, case = auto.auto_values(develop.apply_crop(rgb, state['crop']))
    log.info("Auto on %s: case=%s %s", photo.file_name, case, values)
    # Luz e branco saem do Auto; vibrance, saturation, preset, crop e camadas ficam.
    return apply_state(photo, {**state['values'], **values}, state['preset'],
                       state['crop'], state['layers'], HistoryEntry.Kind.AUTO)


def reset(photo):
    """Volta ao neutro — ajustes, preset, crop e camadas."""
    return apply_state(photo, develop.NEUTRAL, '', None, [], HistoryEntry.Kind.RESET)


def _undo_state(entry):
    # Entradas de antes do crop/das camadas nao tem a chave: normalizar da o
    # quadro inteiro e nenhuma camada.
    before = entry.before or {}
    _save_state(entry.photo, normalize_state(
        entry.photo, before.get('values'), before.get('preset', ''), before.get('crop'),
        before.get('layers')))


for _kind in (HistoryEntry.Kind.ADJUST, HistoryEntry.Kind.AUTO,
              HistoryEntry.Kind.PRESET, HistoryEntry.Kind.RESET,
              HistoryEntry.Kind.CROP, HistoryEntry.Kind.LAYER):
    history.register_undo(_kind, _undo_state)

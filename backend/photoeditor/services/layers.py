"""Camadas: mascaras gravadas, selecao por pincel e o render com camadas.

A mascara e um PNG em tons de cinza (0 = fora, 255 = dentro, borda macia) em
``MASKS_DIR``, na resolucao do PREVIEW e sobre a foto INTEIRA — antes do crop,
como o quadro do crop. O nome do arquivo e o hash do conteudo: gravar a mesma
mascara duas vezes da o mesmo arquivo, e uma mascara nunca muda. Por isso o
historico so guarda a chave, e desfazer volta a apontar para a antiga.

A selecao e incremental: cada traco chega com a mascara que ja existe
(``base``) e sai uma nova, somando (``add``) ou tirando (``subtract``) o
objeto sob o traco.
"""
import hashlib
import io
import logging
from functools import lru_cache

import cv2
import numpy as np
from django.conf import settings
from PIL import Image

from photoeditor.imaging import develop, segment
from photoeditor.services import derivatives

log = logging.getLogger(__name__)

MODES = ('add', 'subtract')


def mask_path(key):
    return settings.MASKS_DIR / f'{key}.png'


def save_mask(alpha) -> str:
    """Grava a mascara (float em [0, 1]) e devolve a chave."""
    u8 = (np.clip(alpha, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    buf = io.BytesIO()
    Image.fromarray(u8, mode='L').save(buf, format='PNG', optimize=True)
    data = buf.getvalue()
    key = hashlib.sha1(data).hexdigest()[:develop.MASK_KEY_LEN]
    path = mask_path(key)
    if not path.is_file():
        settings.MASKS_DIR.mkdir(parents=True, exist_ok=True)
        derivatives.write_atomic(path, data)
        load_mask.cache_clear()     # um "nao existe" em cache ficaria velho
    return key


@lru_cache(maxsize=16)
def load_mask(key):
    """Mascara uint8, ou ``None`` se a chave nao existe (so leitura: o array
    e compartilhado pelo cache)."""
    if not develop.is_mask_key(key) or not mask_path(key).is_file():
        return None
    with Image.open(mask_path(key)) as img:
        arr = np.asarray(img.convert('L'))
    arr.flags.writeable = False
    return arr


def overlay_png(key) -> bytes | None:
    """A mascara como PNG branco com transparencia: o frontend pinta a cor
    por cima com CSS (``mask-image``), entao a cor da selecao e do tema."""
    mask = load_mask(key)
    if mask is None:
        return None
    rgba = np.empty(mask.shape + (2,), np.uint8)
    rgba[..., 0] = 255
    rgba[..., 1] = mask
    buf = io.BytesIO()
    Image.fromarray(rgba, mode='LA').save(buf, format='PNG')
    return buf.getvalue()


def _preview_rgb(photo):
    with Image.open(derivatives.preview_path(photo)) as img:
        return np.asarray(img.convert('RGB'))


def _embed_key(photo):
    return f'{photo.pk}-{photo.mtime_ns}'


def warm(photo):
    """Prepara o modelo para a foto — chamado ao entrar no modo selecao, para
    o primeiro traco nao pagar o encoder."""
    if segment.available():
        segment.warm(_embed_key(photo), _preview_rgb(photo))


def apply_stroke(photo, base, points, radius, mode='add', smart=True):
    """Mascara ``base`` (chave ou vazia) + um traco -> (chave nova ou ``None``
    se ficou vazia, se a deteccao de objeto foi usada)."""
    rgb = _preview_rgb(photo)
    smart = bool(smart) and segment.available()
    region = segment.select(_embed_key(photo), rgb, points, radius, smart=smart)

    current = load_mask(base) if base else None
    if current is not None and current.shape == region.shape:
        current = current.astype(np.float32) / 255.0
    else:
        current = np.zeros(region.shape, np.float32)
    if mode == 'subtract':
        out = np.minimum(current, 1.0 - region)
    else:
        out = np.maximum(current, region)
    if not (out > 0.5).any():
        return None, smart
    return save_mask(out), smart


def coverage(key) -> float:
    mask = load_mask(key)
    return float((mask > 127).mean()) if mask is not None else 0.0


# --------------------------------------------------------------------------- #
# Render
# --------------------------------------------------------------------------- #

def _alpha(key, full_shape, crop, out_shape):
    """A mascara levada a foto que vai ser revelada: do tamanho da foto
    inteira, recortada pelo mesmo crop e reduzida junto com ela."""
    mask = load_mask(key)
    if mask is None:
        log.warning("Layer mask %s is missing; the layer is skipped.", key)
        return None
    h, w = full_shape[:2]
    if mask.shape != (h, w):
        mask = cv2.resize(mask, (w, h), interpolation=cv2.INTER_LINEAR)
    mask = develop.apply_crop(mask, crop)
    oh, ow = out_shape[:2]
    if mask.shape != (oh, ow):
        mask = cv2.resize(mask, (ow, oh), interpolation=cv2.INTER_AREA)
    return mask.astype(np.float32) / 255.0


def develop_image(rgb, state, fit=None):
    """Foto INTEIRA (antes do crop) + estado -> foto editada.

    Crop primeiro, depois ``fit`` (a reducao da exportacao), depois o motor —
    a mesma ordem para o preview e para a exportacao."""
    out = develop.apply_crop(rgb, state['crop'])
    if fit is not None:
        out = fit(out)
    layers = []
    for layer in state.get('layers') or ():
        alpha = _alpha(layer['mask'], rgb.shape, state['crop'], out.shape)
        if alpha is not None:
            layers.append((layer['values'], layer['preset'], alpha))
    return develop.render_layers(out, state['values'], state['preset'], layers)

"""Formato de publicacao das fotos exportadas (TODO 7.2).

Porte do ``src/photofix/publish.py`` do ``../correct-photos``, que espelha o
``convert()`` do PhotoE: JPEG numa caixa 1920x1080 (na orientacao da foto),
72 dpi, qualidade 88, ``optimize`` e ``progressive``. Duas diferencas
deliberadas em relacao ao PhotoE, as mesmas do original:

* o EXIF e PRESERVADO — o PhotoE le DateTimeOriginal + SubsecTimeOriginal +
  OffsetTimeOriginal para ordenar a galeria do evento;
* a miniatura embutida sai (o PhotoE gera a propria) e ``Orientation`` vira 1,
  porque os pixels ja sao girados aqui.

O render acontece DEPOIS de reduzir para a caixa: os ajustes sao operacoes
por pixel (e as de vizinhanca sao medidas numa miniatura de qualquer forma),
entao revelar 24 MP para jogar fora 90 % dos pixels so custaria tempo.
"""
from __future__ import annotations

import io
import logging
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

log = logging.getLogger(__name__)

TARGET_LONG_SIDE = 1920
TARGET_SHORT_SIDE = 1080
TARGET_DPI = 72
JPEG_QUALITY = 88


def target_box(width, height) -> tuple[int, int]:
    """Caixa 1080p na orientacao da imagem (mesma regra do PhotoE)."""
    if height > width:
        return TARGET_SHORT_SIDE, TARGET_LONG_SIDE
    if height == width:
        return TARGET_SHORT_SIDE, TARGET_SHORT_SIDE
    return TARGET_LONG_SIDE, TARGET_SHORT_SIDE


def load(path: Path) -> tuple[np.ndarray, bytes]:
    """(pixels RGB ja girados e na caixa de publicacao, EXIF original)."""
    with Image.open(path) as raw:
        exif = raw.info.get('exif', b'')
        # A DCT reduz por 1/2, 1/4, 1/8 mantendo os dois lados >= o pedido.
        raw.draft('RGB', (TARGET_LONG_SIDE, TARGET_LONG_SIDE))
        img = ImageOps.exif_transpose(raw)
        if img.mode != 'RGB':
            img = img.convert('RGB')
        box = target_box(img.width, img.height)
        if img.width > box[0] or img.height > box[1]:     # nunca amplia
            img = ImageOps.contain(img, box, Image.Resampling.LANCZOS)
        return np.asarray(img), exif


def _exif_for_publication(exif_bytes: bytes) -> bytes | None:
    """EXIF com Orientation=1 e sem miniatura; ``None`` se nao der para ler
    (sem EXIF e melhor que EXIF corrompido)."""
    if not exif_bytes:
        return None
    try:
        import piexif
        d = piexif.load(exif_bytes)
        d['0th'][piexif.ImageIFD.Orientation] = 1
        d['thumbnail'] = None
        d['1st'] = {}
        return piexif.dump(d)
    except Exception:
        log.warning("Could not rewrite EXIF; exporting without it.", exc_info=True)
        return None


def encode(rgb: np.ndarray, exif_bytes: bytes = b'') -> bytes:
    buf = io.BytesIO()
    kw = dict(format='JPEG', quality=JPEG_QUALITY, optimize=True, progressive=True,
              dpi=(TARGET_DPI, TARGET_DPI))
    clean = _exif_for_publication(exif_bytes)
    if clean:
        kw['exif'] = clean
    Image.fromarray(rgb, mode='RGB').save(buf, **kw)
    return buf.getvalue()

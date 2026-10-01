"""Formato de publicacao das fotos exportadas (TODO 7.2).

Porte do ``src/photofix/publish.py`` do ``../correct-photos``, que espelha o
``convert()`` do PhotoE: JPEG numa caixa 1920x1080 (na orientacao da foto),
72 dpi, qualidade 88, ``optimize`` e ``progressive``. Duas diferencas
deliberadas em relacao ao PhotoE, as mesmas do original:

* o EXIF e PRESERVADO — o PhotoE le DateTimeOriginal + SubsecTimeOriginal +
  OffsetTimeOriginal para ordenar a galeria do evento;
* a miniatura embutida sai (o PhotoE gera a propria) e ``Orientation`` vira 1,
  porque os pixels ja sao girados aqui.

O ``Software`` do EXIF passa a identificar este editor: nome, versao e URL.

O render acontece DEPOIS de reduzir para a caixa: os ajustes sao operacoes
por pixel (e as de vizinhanca sao medidas numa miniatura de qualquer forma),
entao revelar 24 MP para jogar fora 90 % dos pixels so custaria tempo.
"""
from __future__ import annotations

import io
import logging
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageOps

log = logging.getLogger(__name__)

TARGET_LONG_SIDE = 1920
TARGET_SHORT_SIDE = 1080
TARGET_DPI = 72
JPEG_QUALITY = 88

# Versao Instagram: 1080 px de largura — a resolucao do feed — com a altura
# da proporcao (1080x1350 em 4:5, 1080x1080, 1080x566 em 1,91:1). Tamanho
# EXATO: o Instagram reamostra o que chega fora disso. A qualidade e mais
# alta porque o proprio Instagram recomprime.
INSTAGRAM_WIDTH = 1080
INSTAGRAM_QUALITY = 92


def target_box(width, height) -> tuple[int, int]:
    """Caixa 1080p na orientacao da imagem (mesma regra do PhotoE)."""
    if height > width:
        return TARGET_SHORT_SIDE, TARGET_LONG_SIDE
    if height == width:
        return TARGET_SHORT_SIDE, TARGET_SHORT_SIDE
    return TARGET_LONG_SIDE, TARGET_SHORT_SIDE


def load(path: Path, crop_scale: float = 1.0,
         long_side: int = TARGET_LONG_SIDE) -> tuple[np.ndarray, bytes]:
    """(pixels RGB ja girados, EXIF original).

    A resolucao e a menor que ainda enche a caixa de publicacao DEPOIS do crop:
    um recorte de 50 % precisa de 3840 px no lado maior para sair em 1920.
    ``crop_scale`` e a fracao do lado menor da foto que o quadro ocupa.
    """
    need = int(math.ceil(long_side / max(crop_scale, 0.01)))
    with Image.open(path) as raw:
        exif = raw.info.get('exif', b'')
        # A DCT reduz por 1/2, 1/4, 1/8 mantendo os dois lados >= o pedido.
        raw.draft('RGB', (need, need))
        img = ImageOps.exif_transpose(raw)
        if img.mode != 'RGB':
            img = img.convert('RGB')
        return np.asarray(img), exif


def fit(rgb: np.ndarray) -> np.ndarray:
    """Reduz para a caixa de publicacao; nunca amplia."""
    h, w = rgb.shape[:2]
    box = target_box(w, h)
    if w <= box[0] and h <= box[1]:
        return rgb
    img = ImageOps.contain(Image.fromarray(rgb), box, Image.Resampling.LANCZOS)
    return np.asarray(img)


def instagram_size(aspect: float) -> tuple[int, int]:
    """(largura, altura) da versao Instagram para um recorte ``aspect``
    (altura/largura). A altura arredonda PARA CIMA: 1080x565 ja passaria de
    1,91:1 e o Instagram recortaria; 1080x566 fica dentro."""
    return INSTAGRAM_WIDTH, max(math.ceil(INSTAGRAM_WIDTH * aspect - 1e-6), 1)


def fit_exact(rgb: np.ndarray, size: tuple[int, int]) -> np.ndarray:
    """Leva o recorte ao tamanho exato ``size`` (reduz ou amplia). O recorte
    ja tem a proporcao de ``size``; a diferenca e so o arredondamento."""
    h, w = rgb.shape[:2]
    if (w, h) == tuple(size):
        return rgb
    img = Image.fromarray(rgb).resize(size, Image.Resampling.LANCZOS)
    return np.asarray(img)


def software_tag() -> str:
    """Valor do EXIF ``Software``: nome, versao e URL do editor
    (ex.: ``PhotoEditor 1.2.3 (https://github.com/...)``)."""
    from django.conf import settings
    return f'{settings.BRAND_NAME} {settings.VERSION} ({settings.BRAND_URL})'


def _exif_for_publication(exif_bytes: bytes, software: str = '') -> bytes | None:
    """EXIF com Orientation=1, sem miniatura e com ``Software`` = este editor.

    Foto sem EXIF ganha um so com o ``Software``. EXIF que nao da para ler
    tambem vira so o ``Software`` (sem o original e melhor que corrompido)."""
    import piexif
    empty = {'0th': {}, 'Exif': {}, 'GPS': {}, 'Interop': {}, '1st': {}, 'thumbnail': None}
    d = empty
    if exif_bytes:
        try:
            d = piexif.load(exif_bytes)
        except Exception:
            log.warning("Could not read EXIF; exporting without the original.", exc_info=True)
            d = empty
    d['0th'][piexif.ImageIFD.Orientation] = 1
    if software:
        d['0th'][piexif.ImageIFD.Software] = software.encode('ascii', 'replace')
    d['thumbnail'] = None
    d['1st'] = {}
    try:
        return piexif.dump(d)
    except Exception:
        log.warning("Could not rewrite EXIF; exporting without it.", exc_info=True)
        return None


def encode(rgb: np.ndarray, exif_bytes: bytes = b'', software: str = '',
           quality: int = JPEG_QUALITY) -> bytes:
    buf = io.BytesIO()
    kw = dict(format='JPEG', quality=quality, optimize=True, progressive=True,
              dpi=(TARGET_DPI, TARGET_DPI))
    clean = _exif_for_publication(exif_bytes, software)
    if clean:
        kw['exif'] = clean
    Image.fromarray(rgb, mode='RGB').save(buf, **kw)
    return buf.getvalue()

"""Leitura de JPEGs: metadados EXIF e pixels ja na orientacao de exibicao."""
from __future__ import annotations

import datetime
from dataclasses import dataclass
from pathlib import Path

from django.conf import settings
from django.utils import timezone
from PIL import Image, ImageOps

JPEG_SUFFIXES = ('.jpg', '.jpeg')

# Tags EXIF (IFD Exif = 0x8769)
_EXIF_IFD = 0x8769
_ORIENTATION = 0x0112
_DATETIME_ORIGINAL = 0x9003
_OFFSET_TIME_ORIGINAL = 0x9011
_SUBSEC_TIME_ORIGINAL = 0x9291


def is_jpeg(path: Path) -> bool:
    return path.is_file() and path.suffix.lower() in JPEG_SUFFIXES


@dataclass
class JpegInfo:
    width: int
    height: int
    orientation: int
    captured_at: datetime.datetime | None


def _captured_at(exif_ifd) -> datetime.datetime | None:
    raw = exif_ifd.get(_DATETIME_ORIGINAL)
    if not raw:
        return None
    try:
        dt = datetime.datetime.strptime(str(raw).strip('\x00 '), '%Y:%m:%d %H:%M:%S')
    except ValueError:
        return None

    subsec = str(exif_ifd.get(_SUBSEC_TIME_ORIGINAL) or '').strip('\x00 ')
    if subsec.isdigit():
        # "12" = 0,12 s; "123" = 0,123 s — a subsec e a parte fracionaria.
        dt = dt.replace(microsecond=int((subsec + '000000')[:6]))

    offset = str(exif_ifd.get(_OFFSET_TIME_ORIGINAL) or '').strip('\x00 ')
    if len(offset) == 6 and offset[0] in '+-' and offset[3] == ':':
        try:
            sign = 1 if offset[0] == '+' else -1
            delta = datetime.timedelta(hours=int(offset[1:3]), minutes=int(offset[4:6]))
            return dt.replace(tzinfo=datetime.timezone(sign * delta))
        except ValueError:
            pass
    # Sem offset gravado: a camera marca a hora local do evento.
    return timezone.make_aware(dt, timezone.get_default_timezone())


def read_info(path: Path) -> JpegInfo:
    """Dimensoes, orientacao e data de captura, lendo so o cabecalho."""
    with Image.open(path) as img:
        exif = img.getexif()
        orientation = int(exif.get(_ORIENTATION) or 1)
        if orientation not in range(1, 9):
            orientation = 1
        width, height = img.size
        if orientation in (5, 6, 7, 8):          # giro de 90 graus troca os lados
            width, height = height, width
        captured = _captured_at(exif.get_ifd(_EXIF_IFD))
    return JpegInfo(width, height, orientation, captured)


def load_rgb(path: Path, max_side: int | None = None) -> Image.Image:
    """Abre o JPEG em RGB, girado conforme o EXIF e reduzido a ``max_side``.

    ``draft`` faz o decodificador JPEG reduzir na propria DCT (1/2, 1/4, 1/8):
    abrir 24 MP para gerar uma miniatura custa uma fracao do tempo.
    """
    img = Image.open(path)
    if max_side:
        img.draft('RGB', (max_side, max_side))
    img = ImageOps.exif_transpose(img)
    if img.mode != 'RGB':
        img = img.convert('RGB')
    if max_side:
        img.thumbnail((max_side, max_side), Image.Resampling.LANCZOS)
    return img


def raw_path(photo) -> Path:
    return settings.RAW_DIR / photo.file_name


def deleted_path(photo) -> Path:
    return settings.DELETED_DIR / (photo.deleted_file_name or photo.file_name)

"""Regras da pasta de projeto que o shell aplica antes de abrir (sem Qt).

* ``is_project``         — a pasta ja e um projeto do editor?
* ``loose_jpegs``        — JPEGs soltos na raiz da pasta (fora de ``raw/``);
* ``adopt_loose_photos`` — cria ``raw/`` e MOVE esses JPEGs para ela.

Mover (nao copiar) e o pedido do fluxo "Novo projeto" numa pasta de fotos: o
evento passa a ser a pasta, com os originais em ``raw/`` — sem duplicar
gigabytes. Mover dentro da mesma pasta e um ``rename`` (instantaneo); nada e
sobrescrito: um nome que ja exista em ``raw/`` fica onde estava e e relatado.
"""
import logging
import os
from pathlib import Path

log = logging.getLogger(__name__)

JPEG_SUFFIXES = ('.jpg', '.jpeg')


def project_root(path: Path) -> Path:
    """Escolheu a propria ``raw/`` de um projeto? O projeto e a pasta de cima."""
    path = Path(path).expanduser()
    if path.name.lower() == 'raw' and not (path / 'raw').is_dir():
        return path.parent
    return path


def is_project(path: Path) -> bool:
    """Ja e um projeto: tem o catalogo do editor ou a pasta de originais."""
    path = Path(path)
    return (path / 'project_data' / 'db.sqlite3').is_file() or (path / 'raw').is_dir()


def loose_jpegs(path: Path) -> list:
    """JPEGs na raiz de ``path`` (sem os ocultos, como os ``._`` do macOS)."""
    try:
        entries = list(os.scandir(path))
    except OSError:
        return []
    return sorted((Path(e.path) for e in entries
                   if e.is_file() and not e.name.startswith('.')
                   and e.name.lower().endswith(JPEG_SUFFIXES)),
                  key=lambda p: p.name.lower())


def adopt_loose_photos(path: Path):
    """Cria ``raw/`` e move os JPEGs soltos para ela.

    Devolve ``(movidos, falhas)``: nomes movidos e ``(nome, motivo)`` do que
    ficou onde estava.
    """
    raw = Path(path) / 'raw'
    raw.mkdir(exist_ok=True)
    moved, failed = [], []
    for photo in loose_jpegs(path):
        target = raw / photo.name
        if target.exists():
            failed.append((photo.name, 'already exists in raw/'))
            continue
        try:
            os.rename(photo, target)
        except OSError as exc:
            failed.append((photo.name, exc.strerror or str(exc)))
            continue
        moved.append(photo.name)
    log.info("Moved %d photos into %s (%d failed)", len(moved), raw, len(failed))
    return moved, failed

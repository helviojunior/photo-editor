"""Projetos recentes da tela Home, no banco do app: ``~/.photoe/photoe.db``.

Banco PROPRIO, separado do catalogo de cada evento (``<projeto>/project_data/
db.sqlite3``): a lista de projetos e da maquina, nao de um projeto.

``sqlite3`` da biblioteca padrao, SEM Django, porque dois processos usam este
modulo —

* o app desktop (``desktop/window.py``), que registra o projeto ao abri-lo;
* o backend (``views/projects.py``), que le a lista para a Home e remove itens.

WAL + ``timeout``: os dois leem e gravam ao mesmo tempo sem "database is
locked". O esquema e versionado por ``PRAGMA user_version`` e migra de forma
INCREMENTAL (``_MIGRATIONS``): o banco persiste entre versoes do app, entao
passo ja publicado nunca muda — acrescenta-se o proximo.
"""
import os
import sqlite3
from contextlib import closing
from datetime import datetime, timezone
from pathlib import Path

# Mais que isso vira arquivo morto: a Home e para os eventos em andamento.
MAX_PROJECTS = 60

JPEG_SUFFIXES = ('.jpg', '.jpeg')

# Um passo por versao do esquema; o indice + 1 e o user_version resultante.
_MIGRATIONS = [
    # 1: projetos abertos. ``key`` = caminho canonico (normcase), para a mesma
    # pasta nao entrar duas vezes no Windows/macOS, que ignoram maiusculas.
    """
    CREATE TABLE project (
        key        TEXT PRIMARY KEY,
        path       TEXT NOT NULL,
        created_at TEXT NOT NULL,
        opened_at  TEXT NOT NULL
    );
    CREATE INDEX project_opened_at ON project (opened_at DESC);
    """,
]


def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec='seconds')


def _resolve(path) -> Path:
    return Path(path).expanduser().resolve()


def _key(path) -> str:
    return os.path.normcase(str(_resolve(path)))


def connect(db: Path) -> sqlite3.Connection:
    """Abre (e cria/migra, se preciso) o banco do app."""
    db = Path(db)
    db.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(db, timeout=10)
    conn.row_factory = sqlite3.Row
    conn.execute('PRAGMA journal_mode=WAL')
    version = conn.execute('PRAGMA user_version').fetchone()[0]
    for number, script in enumerate(_MIGRATIONS[version:], start=version + 1):
        with conn:
            conn.executescript(f'BEGIN; {script}; PRAGMA user_version = {number}; COMMIT;')
    return conn


def load(db: Path) -> list:
    """``[{'path', 'opened_at'}]``, do aberto mais recentemente ao mais antigo."""
    with closing(connect(db)) as conn:
        rows = conn.execute(
            'SELECT path, opened_at FROM project ORDER BY opened_at DESC LIMIT ?',
            (MAX_PROJECTS,)).fetchall()
    return [dict(row) for row in rows]


def touch(db: Path, project):
    """Registra ``project`` como aberto agora (entra no topo da lista)."""
    now = _now()
    with closing(connect(db)) as conn, conn:
        conn.execute(
            'INSERT INTO project (key, path, created_at, opened_at) VALUES (?, ?, ?, ?) '
            'ON CONFLICT(key) DO UPDATE SET path = excluded.path, opened_at = excluded.opened_at',
            (_key(project), str(_resolve(project)), now, now))


def remove(db: Path, project):
    """Tira ``project`` da lista. A pasta em si nunca e tocada."""
    with closing(connect(db)) as conn, conn:
        conn.execute('DELETE FROM project WHERE key = ?', (_key(project),))


def find(db: Path, project):
    """O item da lista para ``project``, ou None — so pastas listadas sao servidas."""
    with closing(connect(db)) as conn:
        row = conn.execute('SELECT path, opened_at FROM project WHERE key = ?',
                           (_key(project),)).fetchone()
    return dict(row) if row else None


def raw_photos(project) -> list:
    """Os JPEGs de ``<project>/raw`` (``os.DirEntry``), ordenados pelo nome."""
    raw = Path(project) / 'raw'
    try:
        with os.scandir(raw) as it:
            entries = [e for e in it
                       if e.is_file() and e.name.lower().endswith(JPEG_SUFFIXES)
                       and not e.name.startswith('.')]
    except OSError:
        return []
    return sorted(entries, key=lambda e: e.name.lower())


def summary(item: dict) -> dict:
    """O que o card da Home mostra: nome, se a pasta existe, fotos e tamanho."""
    path = Path(item['path'])
    exists = path.is_dir()
    photos = raw_photos(path) if exists else []
    size = 0
    for entry in photos:
        try:
            size += entry.stat().st_size
        except OSError:
            pass
    return {
        'path': str(path),
        'name': path.name or str(path),
        'opened_at': item.get('opened_at'),
        'exists': exists,
        'has_raw': exists and (path / 'raw').is_dir(),
        'photo_count': len(photos),
        'size_bytes': size,
    }

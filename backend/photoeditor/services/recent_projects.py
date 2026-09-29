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


def _catalog_cover(project):
    """O arquivo em raw/ da foto marcada como capa no catalogo do projeto.

    Le o ``project_data/db.sqlite3`` do evento SOMENTE LEITURA (``mode=ro``):
    a Home nao abre o projeto e nao pode travar nem alterar o banco dele. Capa
    que e copia virtual le o JPEG da original. Catalogo antigo (sem a coluna
    ``is_cover``) ou ocupado demais: sem capa marcada.
    """
    db = Path(project) / 'project_data' / 'db.sqlite3'
    if not db.is_file():
        return None
    try:
        conn = sqlite3.connect(f'{db.as_uri()}?mode=ro', uri=True, timeout=2)
        with closing(conn):
            row = conn.execute(
                "SELECT COALESCE(o.file_name, p.file_name) FROM photoeditor_photo p "
                "LEFT JOIN photoeditor_photo o ON o.id = p.copy_of_id "
                "WHERE p.is_cover = 1 AND p.status = 'active' LIMIT 1").fetchone()
    except sqlite3.Error:
        return None
    return Path(project) / 'raw' / row[0] if row else None


def cover_source(project):
    """A foto do card da Home: a capa marcada no editor; sem ela, a primeira
    de raw/ (pelo nome). None se nao ha foto nenhuma."""
    chosen = _catalog_cover(project)
    if chosen is not None and chosen.is_file():
        return chosen
    photos = raw_photos(project)
    return Path(photos[0].path) if photos else None


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
    cover = cover_source(path) if exists else None
    try:
        # Chave da capa (arquivo + mtime): entra na URL da imagem do card, para
        # o navegador nao mostrar a capa antiga depois de trocada.
        cover_key = f'{cover.name}:{cover.stat().st_mtime_ns}' if cover else ''
    except OSError:
        cover_key = ''
    return {
        'path': str(path),
        'name': path.name or str(path),
        'cover': cover_key,
        'opened_at': item.get('opened_at'),
        'exists': exists,
        'has_raw': exists and (path / 'raw').is_dir(),
        'photo_count': len(photos),
        'size_bytes': size,
    }

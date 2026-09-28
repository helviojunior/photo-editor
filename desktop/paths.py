"""Onde fica cada coisa — no repositorio (dev) e no pacote instalado.

O pacote espelha o repositorio: ``<instalacao>/app`` tem ``backend/``,
``desktop/``, ``frontend/build/``, ``models/`` e ``VERSION``; o Python fica em
``<instalacao>/runtime``. Assim o mesmo codigo roda nos dois sem condicional.

O que o app ESCREVE nunca vai para a instalacao (pode ser somente leitura,
ex.: ``Program Files``): vai para ``~/.photoe``.

Sem Qt aqui: o processo do servidor tambem importa este modulo.
"""
import os
from pathlib import Path

APP_NAME = 'PhotoEditor'

APP_ROOT = Path(__file__).resolve().parent.parent
BACKEND_DIR = APP_ROOT / 'backend'
FRONTEND_BUILD_DIR = APP_ROOT / 'frontend' / 'build'
FRONTEND_PUBLIC_DIR = APP_ROOT / 'frontend' / 'public'
SEGMENT_MODEL_DIR = APP_ROOT / 'models' / 'sam2.1-hiera-tiny'

# ~/.photoe nos tres SOs (C:\Users\<voce>\.photoe no Windows): banco do app,
# segredos gerados, perfil do navegador, capas e logs. PHOTOEDITOR_DATA_DIR
# sobrescreve (testes).
DATA_DIR = Path(os.environ.get('PHOTOEDITOR_DATA_DIR') or Path.home() / '.photoe')
LOG_DIR = DATA_DIR / 'logs'
# Perfil do navegador embarcado (cookies, localStorage, cache HTTP).
WEB_PROFILE_DIR = DATA_DIR / 'webengine'
SETTINGS_FILE = DATA_DIR / 'desktop.ini'
# Banco do app: a lista de projetos da Home (services/recent_projects.py).
APP_DB = DATA_DIR / 'photoe.db'


def version() -> str:
    try:
        return (APP_ROOT / 'VERSION').read_text().strip() or '0.0.0'
    except OSError:
        return '0.0.0'


def asset(relative: str) -> Path:
    """Arquivo do frontend (logo, favicon): do build, senao de public/."""
    for base in (FRONTEND_BUILD_DIR, FRONTEND_PUBLIC_DIR):
        path = base / relative
        if path.is_file():
            return path
    return FRONTEND_PUBLIC_DIR / relative

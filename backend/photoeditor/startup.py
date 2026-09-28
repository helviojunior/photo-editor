import os
import sys
import logging
from django.conf import settings

log = logging.getLogger(__name__)

# Flag de módulo para evitar execuções repetidas no mesmo processo
_ALREADY_RAN = False

# Comandos do manage.py que NÃO devem disparar o startup
_SKIP_COMMANDS = {
    "collectstatic", "migrate", "makemigrations", "showmigrations",
    "check", "shell", "dbshell", "inspectdb", "flush",
    "createsuperuser", "changepassword", "compilemessages",
    "makemessages", "squashmigrations", "test", "sendtestemail",
}

# O servidor do app desktop (desktop/server.py) liga esta variavel: ele roda o
# ``migrate`` depois do ``django.setup()`` e so entao chama ``on_startup``.
# Rodar no ``ready()`` catalogaria as fotos num banco ainda sem as tabelas.
DEFER_ENV = "PHOTOEDITOR_DEFER_STARTUP"


def _should_run_now() -> bool:
    """
    Garante que on_startup só execute quando o app está servindo, e não
    durante comandos de build/manage (migrate, etc.).
    """
    if os.environ.get(DEFER_ENV):
        return False

    if len(sys.argv) > 0 and os.path.basename(sys.argv[0]) in ("manage.py", "django-admin"):
        command = sys.argv[1] if len(sys.argv) > 1 else ""
        if command in _SKIP_COMMANDS:
            return False
        # runserver em DEV: só roda no processo filho (evita execução dupla do autoreloader)
        if command == "runserver" and settings.DEBUG:
            return os.environ.get("RUN_MAIN") == "true" or os.environ.get("WERKZEUG_RUN_MAIN") == "true"

    return True


def on_startup(force=False):
    """Tarefas de boot. ``force`` = chamada explicita do servidor desktop."""
    global _ALREADY_RAN
    if _ALREADY_RAN:
        return
    if not force and not _should_run_now():
        return
    _ALREADY_RAN = True

    try:
        log.info("Running startup tasks...")

        # Subpastas de trabalho do projeto (raw/ ausente so gera aviso).
        ensure_project_dirs()

        # Garante o usuario ``admin`` padrao (sem senha) do Django admin publico.
        ensure_admin_user()

        # Cataloga as fotos de raw/ (idempotente).
        scan_catalog()

        log.info("Startup ok.")
    except Exception:
        log.exception("Fail running startup tasks.")


def ensure_project_dirs():
    """Cria as subpastas que o editor grava e confere a pasta de originais.

    ``project_data/`` ja nasce nas settings (o SQLite precisa dela antes do
    migrate); aqui entram ``deleted/`` e ``publicar/``. ``raw/`` NAO e criada:
    pasta vazia criada por nos esconderia a escolha da pasta errada — melhor
    dizer no log que as fotos nao foram encontradas. (Quem pode criar e o app
    desktop, depois de perguntar a pessoa.)
    """
    for path in (settings.PROJECT_DATA_DIR, settings.DELETED_DIR, settings.PUBLISH_DIR):
        try:
            path.mkdir(parents=True, exist_ok=True)
        except OSError:
            log.exception("Could not create project folder %s.", path)

    if not settings.RAW_DIR.is_dir():
        log.error(
            "Original photos folder not found: %s. Put the JPEGs in <project>/raw.",
            settings.RAW_DIR,
        )


def scan_catalog():
    """Sincroniza o catalogo com raw/. Best-effort: falha so vai para o log."""
    from photoeditor.services import catalog

    try:
        catalog.scan()
    except Exception:
        log.exception("Catalog scan failed.")


def ensure_admin_user():
    """Cria o usuario ``admin`` padrao do Django admin publico, se faltar.

    O sistema nao tem autenticacao: o admin e aberto e o middleware
    ``PublicAdminMiddleware`` entra como este usuario, que nao tem senha.
    Best-effort: qualquer falha aqui e logada e nao derruba o boot.
    """
    from photoeditor.middleware import get_default_admin

    try:
        get_default_admin()
    except Exception:
        # Banco ainda sem as tabelas (boot antes do migrate) — o middleware
        # cria o usuario na primeira visita ao admin.
        log.exception("Could not ensure the default admin user.")

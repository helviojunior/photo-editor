#!/usr/bin/env bash
set -e

export DJANGO_SETTINGS_MODULE="${DJANGO_SETTINGS_MODULE:-core.settings}"

# Banco = SQLite em /project/project_data (ver core/settings.py): nao ha
# servico para esperar. Sem /project montado as settings falham aqui mesmo,
# com a mensagem de como montar.
echo "==> Updating database (makemigrations + migrate)..."
python manage.py makemigrations --noinput
python manage.py migrate --noinput

# O usuario ``admin`` (sem senha) do Django admin publico NAO e criado aqui:
# quem garante e o init da app (photoeditor/startup.py:ensure_admin_user) e,
# na falta dele, o PublicAdminMiddleware na primeira visita a /admin/.

echo "==> Collecting static files..."
python manage.py collectstatic --noinput 2>/dev/null || true

echo "==> Starting application..."
exec "$@"

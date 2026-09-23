#!/usr/bin/env bash
set -e

export DJANGO_SETTINGS_MODULE="${DJANGO_SETTINGS_MODULE:-core.settings}"

echo "==> Waiting for database..."
until python -c "
import os
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'core.settings')
import django
django.setup()
from django.db import connections
connections['default'].ensure_connection()
" 2>/dev/null; do
    echo "    Database unavailable, retrying in 2s..."
    sleep 2
done
echo "==> Database is ready."

echo "==> Checking migrations..."
python manage.py makemigrations --noinput
python manage.py showmigrations --list 2>&1 | grep '\[ \]' && PENDING=1 || PENDING=0

if [ "$PENDING" = "1" ]; then
    echo "==> Applying pending migrations..."
    python manage.py migrate --noinput
else
    echo "==> All migrations are up to date."
fi

# O usuario ``admin`` (sem senha) do Django admin publico NAO e criado aqui:
# quem garante e o init da app (photoeditor/startup.py:ensure_admin_user) e,
# na falta dele, o PublicAdminMiddleware na primeira visita a /admin/.

echo "==> Collecting static files..."
python manage.py collectstatic --noinput 2>/dev/null || true

echo "==> Starting application..."
exec "$@"

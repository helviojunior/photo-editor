"""Django admin publico.

O sistema nao tem autenticacao: o admin tambem e aberto. Toda requisicao em
``/admin/`` sem sessao entra automaticamente como o usuario ``admin`` padrao,
que nao tem senha (``set_unusable_password``) — nao ha tela de login.
"""
import logging

from django.contrib.auth import get_user_model, login

log = logging.getLogger(__name__)

DEFAULT_ADMIN_USERNAME = 'admin'
# Mesmo prefixo montado em core/urls.py.
ADMIN_PREFIX = '/admin/'


def get_default_admin():
    """Devolve o usuario ``admin`` padrao, criando-o se preciso.

    Os flags sao reaplicados a cada chamada: se alguem desligar ``is_staff``
    pelo proprio admin, o acesso nao fica trancado para sempre.
    """
    User = get_user_model()
    user, created = User.objects.get_or_create(
        username=DEFAULT_ADMIN_USERNAME,
        defaults={'is_staff': True, 'is_superuser': True, 'is_active': True},
    )
    if created:
        user.set_unusable_password()
        user.save(update_fields=['password'])
        log.info("Default admin user '%s' created (no password).", DEFAULT_ADMIN_USERNAME)
    elif not (user.is_staff and user.is_superuser and user.is_active):
        user.is_staff = user.is_superuser = user.is_active = True
        user.save(update_fields=['is_staff', 'is_superuser', 'is_active'])
    return user


class PublicAdminMiddleware:
    """Loga como o ``admin`` padrao qualquer visita ao Django admin.

    Precisa vir DEPOIS do ``AuthenticationMiddleware`` (usa ``request.user``).
    """

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        if request.path.startswith(ADMIN_PREFIX) and not request.user.is_authenticated:
            login(request, get_default_admin(),
                  backend='django.contrib.auth.backends.ModelBackend')
        return self.get_response(request)

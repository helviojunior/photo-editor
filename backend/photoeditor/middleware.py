"""Token da sessao do app desktop e Django admin publico.

O sistema nao tem autenticacao de USUARIO: o admin tambem e aberto. Toda
requisicao em ``/admin/`` sem sessao entra automaticamente como o usuario
``admin`` padrao, que nao tem senha (``set_unusable_password``) — nao ha tela
de login.

O que existe e uma trava de TRANSPORTE: o servidor escuta em 127.0.0.1, onde
qualquer programa da maquina — e qualquer site aberto no navegador comum da
pessoa — conseguiria chamar a API. O ``AppTokenMiddleware`` so deixa passar
quem traz o token sorteado pelo launcher a cada execucao, que so o navegador
embarcado recebe.
"""
import logging
import secrets

from django.conf import settings
from django.contrib.auth import get_user_model, login
from django.http import JsonResponse

log = logging.getLogger(__name__)

# Repetidos em desktop/server.py (quem entrega o token ao navegador embarcado).
APP_TOKEN_COOKIE = 'photoeditor_token'
APP_TOKEN_HEADER = 'HTTP_X_PHOTOEDITOR_TOKEN'


class AppTokenMiddleware:
    """Recusa (403) toda requisicao sem o token da sessao do app.

    Aceita o cookie (o navegador embarcado) ou o cabecalho ``X-PhotoEditor-Token``
    (o proprio launcher, ao checar se o servidor subiu). Sem ``APP_TOKEN``
    configurado (``manage.py runserver`` em dev) nao verifica nada.
    """

    def __init__(self, get_response):
        self.get_response = get_response
        self.token = getattr(settings, 'APP_TOKEN', '') or ''

    def __call__(self, request):
        if self.token:
            given = (request.COOKIES.get(APP_TOKEN_COOKIE)
                     or request.META.get(APP_TOKEN_HEADER) or '')
            if not secrets.compare_digest(given.encode(), self.token.encode()):
                log.warning("Request without the app token refused: %s %s",
                            request.method, request.path)
                return JsonResponse({'error': 'Forbidden'}, status=403)
        return self.get_response(request)

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

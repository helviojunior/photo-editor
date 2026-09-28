"""index.html do build do React para toda rota que nao e arquivo nem API.

Os arquivos do build (``/static/js/...``, ``/assets/...``, ``/favicon.png``)
saem antes, pelo WhiteNoise (``WHITENOISE_ROOT``); o que chega aqui e rota do
React Router (``/photos/<id>``), que so existe no navegador.
"""
from django.conf import settings
from django.http import FileResponse, JsonResponse


def spa_index(request, path=''):
    index = settings.FRONTEND_BUILD_DIR / 'index.html'
    if not index.is_file():
        return JsonResponse(
            {'error': f'Frontend build not found in {settings.FRONTEND_BUILD_DIR}'},
            status=404)
    response = FileResponse(open(index, 'rb'), content_type='text/html; charset=utf-8')
    # O index aponta para os bundles com hash do build: nunca guardar em cache,
    # senao o app atualizado abre com o JS da versao anterior.
    response['Cache-Control'] = 'no-cache'
    return response

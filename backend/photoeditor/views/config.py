from django.conf import settings
from rest_framework.permissions import AllowAny
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.i18n import DEFAULT_LANGUAGE, SUPPORTED_LANGUAGES, normalize_language


class AppConfigView(APIView):
    """Configuracao publica consumida pelo frontend no carregamento.

    ``default_language`` e o terceiro degrau da resolucao de idioma
    (cookie -> navegador -> padrao do sistema), ver frontend/src/lib/language.js.
    """
    permission_classes = [AllowAny]

    def get(self, request):
        return Response({
            'default_language': normalize_language(
                getattr(settings, 'DEFAULT_LANGUAGE', DEFAULT_LANGUAGE)),
            'supported_languages': list(SUPPORTED_LANGUAGES),
            'brand_name': getattr(settings, 'BRAND_NAME', 'PhotoEditor'),
            'version': getattr(settings, 'VERSION', ''),
        })

from django.urls import path, re_path
from django.views.generic import RedirectView

from photoeditor.views.config import AppConfigView


app_name = 'photoeditor'

favicon_view = RedirectView.as_view(url='/static/favicon.png', permanent=True)

urlpatterns = [

    # General
    re_path(r'^favicon\.ico', favicon_view),
    re_path(r'^favicon\.png', favicon_view),

    # Configuracao publica do frontend (idioma padrao, versao)
    path('api/config/', AppConfigView.as_view(), name='app-config'),

]

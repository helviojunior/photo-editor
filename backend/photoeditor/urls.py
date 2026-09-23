from django.urls import path, re_path
from django.views.generic import RedirectView

from photoeditor.views.config import AppConfigView
from photoeditor.views.photos import (
    PhotoListView, PhotoDetailView, PhotoRescanView,
    PhotoThumbnailView, PhotoPreviewView, PhotoOriginalView,
)


app_name = 'photoeditor'

favicon_view = RedirectView.as_view(url='/static/favicon.png', permanent=True)

urlpatterns = [

    # General
    re_path(r'^favicon\.ico', favicon_view),
    re_path(r'^favicon\.png', favicon_view),

    # Configuracao publica do frontend (idioma padrao, versao)
    path('api/config/', AppConfigView.as_view(), name='app-config'),

    # Catalogo de fotos
    path('api/photos/', PhotoListView.as_view(), name='photo-list'),
    path('api/photos/rescan/', PhotoRescanView.as_view(), name='photo-rescan'),
    path('api/photos/<uuid:pk>/', PhotoDetailView.as_view(), name='photo-detail'),
    path('api/photos/<uuid:pk>/thumbnail/', PhotoThumbnailView.as_view(), name='photo-thumbnail'),
    path('api/photos/<uuid:pk>/preview/', PhotoPreviewView.as_view(), name='photo-preview'),
    path('api/photos/<uuid:pk>/original/', PhotoOriginalView.as_view(), name='photo-original'),

]

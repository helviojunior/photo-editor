"""
URL configuration for the project.

The `urlpatterns` list routes URLs to views. For more information please see:
    https://docs.djangoproject.com/en/5.2/topics/http/urls/
"""
from django.contrib import admin
from django.urls import path, include, re_path

from photoeditor.views.spa import spa_index

urlpatterns = [
    path('admin/', admin.site.urls),
    path('', include('photoeditor.urls')),
    # Por ultimo: rota do React Router devolve o index.html do build. API e
    # admin ficam de fora, para uma URL errada ali responder 404 em JSON.
    # /__desktop__/ e acao para o app desktop, que a intercepta antes de a
    # navegacao sair; se chegar aqui, nao ha app em volta.
    re_path(r'^(?!api/|admin/|django-static/|__desktop__/)(?P<path>.*)$', spa_index, name='spa'),
]

#Custom Handlers
handler404 = 'photoeditor.views.handlers.handler404'
handler500 = 'photoeditor.views.handlers.handler500'
handler403 = 'photoeditor.views.handlers.handler500'
handler400 = 'photoeditor.views.handlers.handler500'

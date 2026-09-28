"""Tela Home: projetos recentes e a capa de cada card.

A lista mora no banco do app, ``~/.photoe/photoe.db`` (ver
``services/recent_projects.py``). Abrir um projeto NAO passa por aqui: quem
escolhe a pasta e reinicia o servidor e o app desktop (``/__desktop__/open``).
"""
import hashlib
import logging

from django.conf import settings
from django.http import FileResponse, Http404
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.imaging.io import load_rgb
from photoeditor.services import recent_projects
from photoeditor.services.derivatives import write_atomic, _encode

log = logging.getLogger(__name__)

COVER_SIDE = 480
COVER_QUALITY = 82


def _listed(request):
    """O item da lista para o ``path`` pedido — nunca serve pasta fora dela."""
    path = (request.query_params.get('path') or request.data.get('path') or '').strip()
    item = recent_projects.find(settings.APP_DB, path) if path else None
    if item is None:
        raise Http404
    return item


class RecentProjectsView(APIView):
    """GET: os cards da Home. DELETE ``{path}``: tira da lista (a pasta fica)."""

    def get(self, request):
        items = recent_projects.load(settings.APP_DB)
        return Response({
            'projects': [recent_projects.summary(item) for item in items],
            'current': None if settings.HOME_MODE else str(settings.PROJECT_ROOT),
        })

    def delete(self, request):
        item = _listed(request)
        recent_projects.remove(settings.APP_DB, item['path'])
        return Response(status=204)


class ProjectCoverView(APIView):
    """Capa do card: a primeira foto de ``raw/``, reduzida.

    Fica em cache na pasta de dados do app (nao na do projeto: a Home nao
    escreve em pasta de evento que nao esta aberta). O nome carrega a foto e
    o ``mtime`` dela, entao trocar as fotos troca a capa.
    """

    def get(self, request):
        item = _listed(request)
        photos = recent_projects.raw_photos(item['path'])
        if not photos:
            raise Http404
        first = photos[0]
        tag = hashlib.sha1(
            f"{item['path']}|{first.name}|{first.stat().st_mtime_ns}".encode()
        ).hexdigest()[:20]
        path = settings.PROJECT_COVERS_DIR / f'{tag}.jpg'
        if not path.is_file():
            path.parent.mkdir(parents=True, exist_ok=True)
            try:
                img = load_rgb(first.path, max_side=COVER_SIDE)
            except Exception:
                log.warning("Could not read the cover photo %s.", first.path, exc_info=True)
                raise Http404
            write_atomic(path, _encode(img, COVER_QUALITY))
        response = FileResponse(open(path, 'rb'), content_type='image/jpeg')
        response['Cache-Control'] = 'private, max-age=86400'
        return response

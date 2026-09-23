from django.http import FileResponse, Http404
from django.shortcuts import get_object_or_404
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.imaging.io import raw_path
from photoeditor.models import Photo
from photoeditor.services import catalog, derivatives

# URLs de imagem carregam a versao (mtime / hash dos ajustes): o conteudo de
# uma URL nunca muda, entao o navegador pode guardar para sempre.
IMMUTABLE = 'public, max-age=31536000, immutable'


def photo_json(photo):
    return {
        'id': str(photo.pk),
        'file_name': photo.file_name,
        'width': photo.width,
        'height': photo.height,
        'captured_at': photo.captured_at.isoformat() if photo.captured_at else None,
        'thumbnail_url': f'/api/photos/{photo.pk}/thumbnail/?v={photo.mtime_ns}',
        'preview_url': f'/api/photos/{photo.pk}/preview/?v={photo.mtime_ns}',
    }


def active_photo(pk):
    """Foto ativa (em raw/); excluida ou sumida responde 404."""
    return get_object_or_404(Photo, pk=pk, status=Photo.Status.ACTIVE)


def image_response(path, cache=IMMUTABLE):
    response = FileResponse(open(path, 'rb'), content_type='image/jpeg')
    response['Cache-Control'] = cache
    return response


class PhotoListView(APIView):
    """Fotos ativas na ordem de captura — a sequencia da filmstrip."""

    def get(self, request):
        photos = Photo.objects.filter(status=Photo.Status.ACTIVE)
        return Response({'results': [photo_json(p) for p in photos]})


class PhotoDetailView(APIView):
    def get(self, request, pk):
        return Response(photo_json(active_photo(pk)))


class PhotoRescanView(APIView):
    def post(self, request):
        return Response(catalog.scan())


class PhotoThumbnailView(APIView):
    def get(self, request, pk):
        return image_response(derivatives.thumbnail_path(active_photo(pk)))


class PhotoPreviewView(APIView):
    def get(self, request, pk):
        return image_response(derivatives.preview_path(active_photo(pk)))


class PhotoOriginalView(APIView):
    def get(self, request, pk):
        path = raw_path(active_photo(pk))
        if not path.is_file():
            raise Http404
        return image_response(path, cache='no-cache')

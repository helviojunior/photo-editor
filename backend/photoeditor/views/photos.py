from urllib.parse import urlencode

from django.http import FileResponse, Http404
from django.shortcuts import get_object_or_404
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.i18n import tr
from photoeditor.imaging import develop
from photoeditor.imaging.io import raw_path
from photoeditor.models import Photo
from photoeditor.services import catalog, derivatives, editing, export, history, trash

# URLs de imagem carregam a versao (mtime / hash dos ajustes): o conteudo de
# uma URL nunca muda, entao o navegador pode guardar para sempre.
IMMUTABLE = 'public, max-age=31536000, immutable'


def render_url(photo, state):
    """URL do preview editado: TODOS os insumos do render vao na query (versao
    do arquivo, do motor e cada ajuste), entao ela nunca muda de conteudo.
    O frontend monta a mesma URL enquanto o slider e arrastado."""
    params = {'v': photo.mtime_ns, 'e': develop.ENGINE_VERSION}
    params.update({k: v for k, v in state['values'].items() if v})
    if state['preset']:
        params['preset'] = state['preset']
    return f'/api/photos/{photo.pk}/render/?{urlencode(params)}'


def photo_json(photo):
    state = editing.get_state(photo)
    return {
        'id': str(photo.pk),
        'file_name': photo.file_name,
        'width': photo.width,
        'height': photo.height,
        'captured_at': photo.captured_at.isoformat() if photo.captured_at else None,
        'thumbnail_url': f'/api/photos/{photo.pk}/thumbnail/?v={photo.mtime_ns}',
        'preview_url': f'/api/photos/{photo.pk}/preview/?v={photo.mtime_ns}',
        'edited_url': render_url(photo, state),
        'adjustments': state,
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
        photos = Photo.objects.filter(status=Photo.Status.ACTIVE).select_related('adjustment')
        return Response({'results': [photo_json(p) for p in photos]})


def action_error(request, exc, status=409):
    return Response({'error': tr(request, exc.key, **exc.params)}, status=status)


class PhotoDetailView(APIView):
    def get(self, request, pk):
        return Response(photo_json(active_photo(pk)))

    def delete(self, request, pk):
        """Exclui = move o arquivo para deleted/ (desfazivel)."""
        try:
            trash.delete_photo(active_photo(pk))
        except history.ActionError as exc:
            return action_error(request, exc)
        return Response(status=204)


class PhotoHistoryView(APIView):
    """Historico completo da foto, inclusive o ja desfeito (mais novo antes)."""

    def get(self, request, pk):
        photo = get_object_or_404(Photo, pk=pk)
        return Response({'results': [history.entry_json(e)
                                     for e in photo.history.all()]})


class UndoView(APIView):
    """CTRL/CMD+Z: desfaz a ultima acao, de qualquer foto."""

    def post(self, request):
        try:
            entry = history.undo_last()
        except history.ActionError as exc:
            return action_error(request, exc)
        if entry is None:
            return Response({'undone': None})
        photo = entry.photo
        return Response({
            'undone': history.entry_json(entry),
            'photo': photo_json(photo) if photo.status == Photo.Status.ACTIVE else None,
        })


class PhotoRescanView(APIView):
    def post(self, request):
        return Response(catalog.scan())


class PhotoThumbnailView(APIView):
    def get(self, request, pk):
        return image_response(derivatives.thumbnail_path(active_photo(pk)))


class PhotoPreviewView(APIView):
    def get(self, request, pk):
        return image_response(derivatives.preview_path(active_photo(pk)))


class PhotoRenderView(APIView):
    """Preview editado para os ajustes da QUERY (nao os gravados): e o que o
    slider mostra antes de soltar. Sem ajustes, e o proprio preview."""

    def get(self, request, pk):
        values = {k: request.query_params[k] for k in develop.SLIDERS
                  if k in request.query_params}
        preset = request.query_params.get('preset', '')
        return image_response(derivatives.render_path(active_photo(pk), values, preset))


class DevelopConfigView(APIView):
    """Sliders (limites/passo), presets e versao do motor."""

    def get(self, request):
        return Response(develop.describe())


class PhotoAdjustmentsView(APIView):
    def put(self, request, pk):
        photo = active_photo(pk)
        editing.set_adjustments(photo, request.data.get('values') or {},
                                request.data.get('preset') or '')
        return Response(photo_json(photo))


class PhotoAutoView(APIView):
    def post(self, request, pk):
        photo = active_photo(pk)
        editing.run_auto(photo)
        return Response(photo_json(photo))


class PhotoResetView(APIView):
    def post(self, request, pk):
        photo = active_photo(pk)
        editing.reset(photo)
        return Response(photo_json(photo))


class PhotoOriginalView(APIView):
    def get(self, request, pk):
        path = raw_path(active_photo(pk))
        if not path.is_file():
            raise Http404
        return image_response(path, cache='no-cache')


class ExportView(APIView):
    """GET: progresso da exportacao. POST: dispara (ou devolve a que ja roda)."""

    def get(self, request):
        return Response(export.status())

    def post(self, request):
        return Response(export.start(), status=202)

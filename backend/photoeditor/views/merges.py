from django.http import Http404, HttpResponse
from django.shortcuts import get_object_or_404
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.imaging import develop
from photoeditor.models import Merge
from photoeditor.services import history, merges
from photoeditor.views.photos import IMMUTABLE, action_error


def get_merge(pk):
    return get_object_or_404(Merge.objects.select_related('base'), pk=pk)


class MergeListView(APIView):
    """POST ``{"photos": [id, ...]}``: cria o merge — a primeira foto e a base,
    as outras viram camadas, ja alinhadas a ela."""

    def post(self, request):
        try:
            merge = merges.create((request.data or {}).get('photos'))
        except history.ActionError as exc:
            return action_error(request, exc, status=400)
        return Response(merges.merge_json(merge), status=201)


class MergeDetailView(APIView):
    def get(self, request, pk):
        return Response(merges.merge_json(get_merge(pk)))

    def put(self, request, pk):
        """``{"layers": [{"photo", "mask"?, "opacity"?, "visible"?}]}``."""
        merge = get_merge(pk)
        try:
            merges.update(merge, (request.data or {}).get('layers'))
        except history.ActionError as exc:
            return action_error(request, exc, status=400)
        return Response(merges.merge_json(merge))

    def delete(self, request, pk):
        """Desfaz o merge: as fotos das camadas voltam a filmstrip."""
        merges.dissolve(get_merge(pk))
        return Response(status=204)


class MergeLayerView(APIView):
    """DELETE: tira a foto da composicao (ela volta a filmstrip). Era a
    ultima camada = o merge e desfeito (``{"dissolved": true}``)."""

    def delete(self, request, pk, photo_pk):
        merge = get_merge(pk)
        try:
            dissolved = merges.remove_layer(merge, photo_pk)
        except history.ActionError as exc:
            return action_error(request, exc, status=404)
        if dissolved:
            return Response({'dissolved': True, 'merge': None})
        return Response({'dissolved': False, 'merge': merges.merge_json(merge)})


class MergeCutoutView(APIView):
    """A area de uma camada, alinhada sobre o preview da base (PNG com
    transparencia). ``?mask=<chave>``: a URL carrega tudo de que o conteudo
    depende, entao nunca muda."""

    def get(self, request, pk, photo_pk):
        key = request.query_params.get('mask') or ''
        if not develop.is_mask_key(key):
            raise Http404
        data = merges.cutout_png(get_merge(pk), photo_pk, key)
        if data is None:
            raise Http404
        response = HttpResponse(data, content_type='image/png')
        response['Cache-Control'] = IMMUTABLE
        return response

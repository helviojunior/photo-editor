import json
from urllib.parse import urlencode

from django.http import FileResponse, Http404, HttpResponse
from django.shortcuts import get_object_or_404
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.i18n import tr
from photoeditor.imaging import develop, segment
from photoeditor.imaging.io import raw_path
from photoeditor.models import Photo
from photoeditor.services import catalog, derivatives, editing, export, history, layers, trash

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
    if not develop.is_crop_identity(state['crop']):
        params.update({f'crop_{k}': v for k, v in state['crop'].items()})
    if state['layers']:
        params['layers'] = layers_param(state['layers'])
    return f'/api/photos/{photo.pk}/render/?{urlencode(params)}'


def layers_param(state_layers):
    """Camadas na query do render, no formato curto que o frontend tambem
    monta (``renderUrl.js``): ``[{"m": mascara, "v": ajustes != 0, "p": preset}]``."""
    return json.dumps([{'m': lay['mask'],
                        'v': {k: v for k, v in lay['values'].items() if v},
                        'p': lay['preset']} for lay in state_layers],
                      separators=(',', ':'), sort_keys=True)


def parse_layers_param(raw):
    try:
        items = json.loads(raw) if raw else []
    except ValueError:
        return []
    if not isinstance(items, list):
        return []
    return develop.normalize_layers([
        {'id': f'q{i}', 'mask': it.get('m'), 'values': it.get('v'), 'preset': it.get('p')}
        for i, it in enumerate(items) if isinstance(it, dict)])


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
    slider mostra antes de soltar — e o recorte, enquanto o quadro do crop e
    arrastado sobre a original. Sem ajustes, e o proprio preview."""

    def get(self, request, pk):
        photo = active_photo(pk)
        q = request.query_params
        values = {k: q[k] for k in develop.SLIDERS if k in q}
        preset = q.get('preset', '')
        state = {
            'values': develop.normalize(values),
            'preset': develop.normalize_preset(preset),
            'crop': develop.normalize_crop(
                {k: q[f'crop_{k}'] for k in develop.CROP_IDENTITY if f'crop_{k}' in q},
                editing.aspect(photo)),
            'layers': parse_layers_param(q.get('layers')),
        }
        return image_response(derivatives.render_path(photo, state))


class DevelopConfigView(APIView):
    """Sliders (limites/passo), presets e versao do motor."""

    def get(self, request):
        return Response({**develop.describe(),
                         'layers': {'max': develop.MAX_LAYERS,
                                    'smart_select': segment.available()}})


class PhotoAdjustmentsView(APIView):
    def put(self, request, pk):
        photo = active_photo(pk)
        editing.set_adjustments(photo, request.data.get('values') or {},
                                request.data.get('preset') or '',
                                request.data.get('crop'),
                                request.data.get('layers'))
        return Response(photo_json(photo))


class PhotoAutoView(APIView):
    """Auto da foto ou, com ``{"layer": id}``, so da camada."""

    def post(self, request, pk):
        photo = active_photo(pk)
        editing.run_auto(photo, (request.data or {}).get('layer'))
        return Response(photo_json(photo))


class PhotoSegmentView(APIView):
    """Selecao por pincel (modo selecao do editor).

    ``{"base": chave|null, "stroke": {"points": [[x, y], ...], "radius": r,
    "mode": "add"|"subtract"}, "smart": bool}`` -> ``{"mask", "coverage",
    "smart"}``. Pontos em fracao da foto inteira (antes do crop), raio em
    fracao do lado maior. Sem ``stroke``, so prepara o modelo para a foto.

    Nao grava a edicao: a mascara so vira camada quando o painel manda o
    estado com ela (PUT adjustments), o que entra no historico."""

    def post(self, request, pk):
        photo = active_photo(pk)
        data = request.data or {}
        base = data.get('base') or None
        if base is not None and layers.load_mask(base) is None:
            return Response({'error': tr(request, 'layers.maskNotFound')}, status=400)
        stroke = data.get('stroke')
        if not stroke:
            layers.warm(photo)
            return Response({'mask': base, 'coverage': layers.coverage(base) if base else 0.0,
                             'smart': segment.available()})
        try:
            points = [(min(max(float(x), 0.0), 1.0), min(max(float(y), 0.0), 1.0))
                      for x, y in stroke.get('points') or []][:2000]
            radius = min(max(float(stroke.get('radius') or 0.02), 0.002), 0.5)
        except (TypeError, ValueError):
            points = []
        if not points:
            return Response({'error': tr(request, 'layers.invalidStroke')}, status=400)
        mode = stroke.get('mode') if stroke.get('mode') in layers.MODES else 'add'
        key, smart = layers.apply_stroke(photo, base, points, radius, mode,
                                         data.get('smart', True))
        return Response({'mask': key, 'coverage': layers.coverage(key) if key else 0.0,
                         'smart': smart})


class MaskView(APIView):
    """Mascara como PNG com transparencia, para o overlay da selecao. A
    chave e o hash do conteudo: a URL nunca muda de conteudo."""

    def get(self, request, key):
        data = layers.overlay_png(key)
        if data is None:
            raise Http404
        response = HttpResponse(data, content_type='image/png')
        response['Cache-Control'] = IMMUTABLE
        return response


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

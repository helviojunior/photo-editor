"""Instagram: conta conectada (vale nos dois modos, Home e editor), versao
Instagram de uma foto e o "Publicar no Instagram" (so com projeto aberto)."""
from rest_framework.response import Response
from rest_framework.views import APIView

from photoeditor.i18n import tr
from photoeditor.services import instagram, instagram_account, instagram_publish
from photoeditor.services.history import ActionError
from photoeditor.views.photos import action_error, active_photo, photo_json


def account_json(request):
    """Estado da conta com a mensagem do erro de login ja traduzida."""
    data = instagram_account.status()
    login = data.get('login')
    if login and login.get('error'):
        login['message'] = tr(request, login['error'])
    return data


class InstagramAccountView(APIView):
    """GET: conta conectada e login em andamento. PUT: ``{"caption_template"}``.
    DELETE: desconecta (esquece a sessao)."""

    def get(self, request):
        return Response(account_json(request))

    def put(self, request):
        instagram_account.set_caption_template((request.data or {}).get('caption_template'))
        return Response(account_json(request))

    def delete(self, request):
        instagram_account.disconnect()
        return Response(account_json(request))


class InstagramLoginView(APIView):
    """POST ``{"username", "password"}``: conecta. A resposta sai quando o
    login termina, falha ou pede um codigo (``login.state == "code"``) — ou,
    se demorar, ainda ``running``: GET espera mais um pouco. DELETE cancela."""

    def get(self, request):
        instagram_account.poll_login()
        return Response(account_json(request))

    def post(self, request):
        data = request.data or {}
        try:
            instagram_account.start_login(data.get('username'), data.get('password'))
        except ActionError as exc:
            return action_error(request, exc, status=400)
        return Response(account_json(request))

    def delete(self, request):
        instagram_account.cancel_login()
        return Response(account_json(request))


class InstagramLoginCodeView(APIView):
    """POST ``{"code"}``: o codigo da verificacao em duas etapas ou o que o
    Instagram mandou por e-mail/SMS para confirmar o acesso."""

    def post(self, request):
        try:
            instagram_account.submit_code((request.data or {}).get('code'))
        except ActionError as exc:
            return action_error(request, exc, status=400)
        return Response(account_json(request))


class PhotoInstagramView(APIView):
    """POST: a versao Instagram da foto — criada agora (201, desfazivel) ou a
    que ja existia (200)."""

    def post(self, request, pk):
        try:
            version, created = instagram.create(active_photo(pk))
        except ActionError as exc:
            return action_error(request, exc)
        return Response(photo_json(version), status=201 if created else 200)


def publish_json(request):
    job = instagram_publish.status()
    if job.get('error'):
        job['message'] = tr(request, job['error'])
    return job


class InstagramPublishView(APIView):
    """GET: andamento da publicacao, dados do evento para a legenda e o
    limite do carrossel. POST ``{"photos": [ids], "caption"}``: publica."""

    def get(self, request):
        return Response({'job': publish_json(request),
                         'event': instagram_publish.event_info(),
                         'max_photos': instagram_publish.MAX_PHOTOS,
                         'caption_max': instagram_publish.CAPTION_MAX})

    def post(self, request):
        data = request.data or {}
        try:
            instagram_publish.start(data.get('photos'), data.get('caption'))
        except ActionError as exc:
            return Response({'error': tr(request, exc.key, **exc.params)}, status=400)
        return Response({'job': publish_json(request)}, status=202)

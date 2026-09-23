"""Envio de e-mails localizados.

O sistema e publico e nao tem contas: quem envia informa o idioma do
destinatario (ex.: ``i18n.language_for_request(request)``); sem idioma, EN.

Uso:
    from photoeditor.services import mailer

    mailer.send_localized('pessoa@exemplo.com', 'email.x.subject',
                          body_key='email.x.body', language='pt-br')
"""
import datetime
import logging

from django.conf import settings
from django.core.mail import EmailMultiAlternatives
from django.utils.html import escape

from photoeditor.i18n import normalize_language, translate

log = logging.getLogger(__name__)


def _brand():
    return getattr(settings, 'BRAND_NAME', 'PhotoEditor')


def render(template, language, subject, preheader, context):
    """Renderiza ``template`` dentro do shell da marca (templates/email/).

    Injeta o que TODO e-mail tem — assunto, preheader, rodape, logo — para
    nenhuma mensagem precisar repetir isso.

    Substituiu a montagem por concatenacao de strings em Python: o layout de um
    e-mail e' HTML, e HTML dentro de aspas nao tem realce de sintaxe, nao
    escapa nada por padrao e so se ve renderizado depois de enviar.
    """
    from django.template.loader import render_to_string

    base = {
        'lang': language,
        'subject': subject,
        'preheader': preheader,
        'brand_name': _brand(),
        'logo_url': getattr(settings, 'BRAND_EMAIL_LOGO', '') or '',
        'footer_copyright': translate('email.footer.copyright', language,
                                      brand=_brand(),
                                      year=datetime.date.today().year),
        'footer_automatic': translate('email.footer.automatic', language),
        'app_url': (getattr(settings, 'APP_URL', '') or '').rstrip('/'),
    }
    base.update(context)
    return render_to_string(template, base)


def send(recipient, subject, html, language=None, text_body='', inline_images=None):
    """Envia um e-mail HTML ja renderizado para ``recipient`` (endereco).

    ``inline_images`` e uma lista de ``(content_id, filename, bytes)`` embutidas
    no HTML via ``cid:<content_id>``.
    """
    email = getattr(recipient, 'email', recipient)
    if not email:
        return False

    message = EmailMultiAlternatives(
        subject=subject,
        body=text_body,
        from_email=settings.DEFAULT_FROM_EMAIL,
        to=[email],
    )
    message.attach_alternative(html, 'text/html')

    if inline_images:
        from email.mime.image import MIMEImage

        message.mixed_subtype = 'related'
        for content_id, filename, payload in inline_images:
            mime_img = MIMEImage(payload, _subtype='png')
            mime_img.add_header('Content-ID', f'<{content_id}>')
            mime_img.add_header('Content-Disposition', 'inline', filename=filename)
            message.attach(mime_img)

    try:
        message.send(fail_silently=False)
        return True
    except Exception:
        log.exception('Failed to send e-mail to %s (lang=%s)', email, language)
        return False


def send_localized(recipient, subject_key, body_key, language=None, extra_blocks=(),
                   preheader_key=None, **params):
    """Envia um e-mail traduzido no idioma informado (EN quando ausente)."""
    language = normalize_language(language)
    params.setdefault('brand', _brand())
    params.setdefault('name', recipient)

    subject = translate(subject_key, language, **params)
    preheader = translate(preheader_key, language, **params) if preheader_key else subject
    html = render('email/message.html', language, subject, preheader, {
        'greeting': translate('email.greeting', language,
                              name=escape(params['name'])),
        'blocks': [translate(body_key, language, **params), *extra_blocks],
    })
    return send(recipient, subject, html, language=language)

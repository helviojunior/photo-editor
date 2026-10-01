"""Publicar no Instagram: as versoes Instagram escolhidas, num post so.

Uma foto vira um post simples; de 2 a ``MAX_PHOTOS``, um carrossel na ordem
recebida (a da filmstrip). Antes de enviar, cada versao e exportada para
``publicar/instagram/`` (``export.write_photo``, que pula o que ja esta em
dia): o que vai para o Instagram e exatamente o arquivo da pasta.

Roda numa thread e expoe o progresso em ``status()``, como o Exportar. O
post fica registrado no banco do evento (``InstagramPost``).
"""
import logging
import threading

from django.conf import settings
from django.db import connection
from django.utils import timezone

from photoeditor.models import InstagramPost, Photo
from photoeditor.services import catalog, export, instagram_account, merges
from photoeditor.services.history import ActionError

log = logging.getLogger(__name__)

# Limite do carrossel na API do app (o mesmo da API oficial).
MAX_PHOTOS = 10
# Limite da legenda no Instagram.
CAPTION_MAX = 2200

_lock = threading.Lock()
_state = {'running': False, 'phase': '', 'done': 0, 'total': 0, 'error': '',
          'detail': '', 'post': None, 'started_at': None, 'finished_at': None}


def status() -> dict:
    with _lock:
        return dict(_state)


def candidates():
    """Versoes Instagram que podem ser publicadas: as da filmstrip."""
    return (catalog.visible().filter(instagram_of__isnull=False)
            .exclude(pk__in=merges.hidden_ids()))


def event_info() -> dict:
    """O que o modal usa para preencher a legenda: nome do evento (a pasta do
    projeto), periodo das fotos e quantas sao."""
    photos = catalog.visible().filter(instagram_of__isnull=True) \
        .exclude(pk__in=merges.hidden_ids())
    dates = sorted(p for p in photos.exclude(captured_at__isnull=True)
                   .values_list('captured_at', flat=True))
    return {
        'name': settings.PROJECT_ROOT.name,
        'first': dates[0].isoformat() if dates else None,
        'last': dates[-1].isoformat() if dates else None,
        'photos': photos.count(),
    }


def start(photo_ids, caption) -> dict:
    ids = list(dict.fromkeys(str(p) for p in (photo_ids or [])))
    if len(ids) > MAX_PHOTOS:
        raise ActionError('instagram.tooMany', max=MAX_PHOTOS)
    found = {str(p.pk): p for p in candidates().filter(pk__in=ids)}
    photos = [found[i] for i in ids if i in found]
    if not photos:
        raise ActionError('instagram.noPhotos')
    caption = (caption or '').strip()
    if len(caption) > CAPTION_MAX:
        raise ActionError('instagram.captionTooLong', max=CAPTION_MAX)
    if not instagram_account.status()['connected']:
        raise ActionError('instagram.notConnected')
    with _lock:
        if _state['running']:
            raise ActionError('instagram.publishRunning')
        _state.update(running=True, phase='export', done=0, total=len(photos), error='',
                      detail='', post=None, started_at=timezone.now().isoformat(),
                      finished_at=None)
        snapshot = dict(_state)
    threading.Thread(target=_run, args=([p.pk for p in photos], caption),
                     name='instagram-publish', daemon=True).start()
    return snapshot


def _set(**fields):
    with _lock:
        _state.update(fields)


def _run(ids, caption):
    try:
        paths = []
        for pk in ids:
            photo = Photo.objects.get(pk=pk)
            merge = merges.for_photo(photo)
            export.write_photo(photo, merge)
            paths.append(export.output_path(photo, merges.version(merge)))
            with _lock:
                _state['done'] += 1

        _set(phase='upload')
        cl, username = instagram_account.client()
        log.info("Publishing %d photo(s) to @%s", len(paths), username)
        if len(paths) == 1:
            media = cl.photo_upload(paths[0], caption)
        else:
            media = cl.album_upload(paths, caption)
        instagram_account.save_session(cl)
        code = getattr(media, 'code', '') or ''
        InstagramPost.objects.create(username=username, media_id=str(media.pk or ''),
                                     code=code, caption=caption,
                                     photos=[str(pk) for pk in ids])
        post = {'code': code, 'url': f'https://www.instagram.com/p/{code}/' if code else ''}
        log.info("Published to @%s: %s", username, post['url'] or media.pk)
        _set(post=post)
    except Exception as exc:
        key = instagram_account.error_key(exc)
        if key == 'instagram.failed':
            key = 'instagram.publishFailed'
            log.exception("Instagram publish failed")
        else:
            log.warning("Instagram publish failed: %s: %s", type(exc).__name__, exc)
        _set(error=key, detail=str(exc).strip()[:300] if key == 'instagram.publishFailed' else '')
    finally:
        _set(running=False, finished_at=timezone.now().isoformat())
        connection.close()

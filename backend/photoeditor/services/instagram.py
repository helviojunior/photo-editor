"""Versao Instagram de uma foto (atalho ``I`` no editor).

E uma copia virtual (como a do Duplicar, ``services/copies.py``): le o mesmo
JPEG, nada vai para raw/, e nasce com tudo o que a foto ja tem — ajustes,
preset e camadas. Se a foto e base de um merge, a versao parte do merge
composto dela (``merges.for_photo``), como a propria foto.

O que muda e o crop: o quadro e FORCADO numa proporcao que o feed aceita
(``develop.INSTAGRAM_RATIOS`` — 4:5, 1:1 ou 1,91:1), a mais proxima do recorte
que a foto ja tinha. A pessoa ajusta o quadro e os ajustes com o mesmo painel
do editor; o backend nunca deixa a versao sair dessas proporcoes
(``editing.crop_ratios``).

Uma versao ativa por foto: pedir de novo devolve a que existe. Fica na
filmstrip ao lado da origem (``<nome>_instagram.jpg``), some junto com ela e
o Exportar a grava em ``publicar/instagram/`` — nunca na raiz de publicar/.
Excluir a versao (DEL) a tira da selecao do Instagram; desfazer a criacao
(CTRL/CMD+Z) tambem.
"""
import logging

from django.db import transaction

from photoeditor.imaging import develop
from photoeditor.models import HistoryEntry, Photo
from photoeditor.services import copies, editing, history

log = logging.getLogger(__name__)

SUFFIX = '_instagram'


def version_of(photo):
    """A versao Instagram ativa de ``photo``, ou None."""
    return Photo.objects.filter(instagram_of=photo, status=Photo.Status.ACTIVE).first()


def initial_crop(photo, crop) -> dict:
    """O quadro da versao: a proporcao do Instagram mais perto do recorte que
    a foto ja tem (retrato vira 4:5, paisagem 1,91:1 ou 1:1), no mesmo centro,
    com o mesmo endireitamento e do tamanho que couber."""
    aspect = editing.aspect(photo)
    ratio = develop.closest_ratio(develop.crop_output_aspect(crop, aspect),
                                  develop.INSTAGRAM_RATIOS.values())
    _, rest = develop.split_angle(crop['angle'])
    return {'scale': crop['scale'], 'cx': crop['cx'], 'cy': crop['cy'],
            'angle': rest, 'ratio': ratio}


def create(photo) -> tuple[Photo, bool]:
    """``(versao, criada agora)``. Ja existindo uma ativa, devolve ela."""
    if photo.instagram_of_id:
        raise history.ActionError('instagram.notForVersion')
    existing = version_of(photo)
    if existing is not None:
        return existing, False
    root = photo.copy_of or photo
    with transaction.atomic():
        version = Photo.objects.create(
            file_name=copies.copy_name(photo, SUFFIX), copy_of=root, instagram_of=photo,
            status=Photo.Status.ACTIVE, **{f: getattr(root, f) for f in copies.FILE_FIELDS})
        editing.copy_state(photo, version,
                           crop=initial_crop(photo, editing.get_state(photo)['crop']))
        history.record(version, HistoryEntry.Kind.INSTAGRAM,
                       before={'status': Photo.Status.DELETED},
                       after={'status': Photo.Status.ACTIVE, 'instagram_of': str(photo.pk)})
    log.info("Instagram version of %s -> %s", photo.file_name, version.file_name)
    return version, True


def _undo_create(entry):
    photo = entry.photo
    photo.status = Photo.Status.DELETED
    photo.save(update_fields=['status', 'updated'])


history.register_undo(HistoryEntry.Kind.INSTAGRAM, _undo_create)

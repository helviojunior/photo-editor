"""Merge de fotos: criar (com a analise de alinhamento), editar camadas,
recortar para o preview e compor a foto que a edicao e a exportacao usam.

Fluxo: a pessoa marca as fotos na filmstrip; a primeira e a BASE (a foto
inteira, embaixo) e cada uma das outras vira uma camada. Ao criar, cada foto
e alinhada a base (``imaging/merge.align``). Depois, em cada camada, a pessoa
seleciona com o pincel a area que quer trazer (a bola) — a mesma selecao do
editor, sobre o preview da foto de origem — e escolhe a transparencia.

Na tela do merge, o preview e montado no NAVEGADOR: a base e, por cima, um
PNG por camada com a area ja alinhada e o fundo transparente
(``cutout_png``). Mudar a transparencia e so CSS, sem ida ao servidor.

Nada e gravado em raw/. A BASE passa a ser a foto do merge: no editor, a
"Editada" parte do merge composto (``edit_source``) e os ajustes, o crop e o
Auto valem sobre ele; o Exportar compoe na resolucao cheia (``render_full``)
e grava ``<base>_merge.jpg`` em publicar/. As fotos das camadas saem da
filmstrip e do Exportar (``hidden_ids``) enquanto o merge existir — nao ha
como excluir por engano uma foto de que ele depende. Desfazer o merge
(``dissolve``) as devolve.
"""
import hashlib
import io
import json
import logging
import uuid
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

from photoeditor.imaging import merge as mergeimg
from photoeditor.imaging.io import load_rgb, raw_path
from photoeditor.models import Merge, Photo
from photoeditor.services import derivatives, layers
from photoeditor.services.history import ActionError

log = logging.getLogger(__name__)

MAX_SOURCES = 16
EXPORT_SUFFIX = '_merge'
# Opacidade inicial, da base a ultima camada: o objeto da base bem claro e
# cada foto seguinte mais forte — a trajetoria "chega" na ultima posicao.
FIRST_OPACITY = 0.25
LAST_OPACITY = 1.0
# Margem (px da foto inteira) em volta da area ao recortar a origem.
CROP_MARGIN = 16


def default_opacities(n):
    """``n`` opacidades de ``FIRST_OPACITY`` a ``LAST_OPACITY``, em rampa."""
    if n <= 1:
        return [LAST_OPACITY] * n
    step = (LAST_OPACITY - FIRST_OPACITY) / (n - 1)
    return [round(FIRST_OPACITY + i * step, 2) for i in range(n)]


def full_size(photo):
    return photo.width, photo.height


def _preview_rgb(photo):
    with Image.open(derivatives.preview_path(photo)) as img:
        return np.asarray(img.convert('RGB'))


def _size(rgb):
    return rgb.shape[1], rgb.shape[0]


# --------------------------------------------------------------------------- #
# Criar e editar
# --------------------------------------------------------------------------- #

def create(photo_ids) -> Merge:
    """Merge novo: a primeira foto e a base, as outras sao as camadas, na
    ordem recebida (a da filmstrip)."""
    ids = list(dict.fromkeys(str(p) for p in (photo_ids or [])))[:MAX_SOURCES + 1]
    photos = {str(p.pk): p for p in Photo.objects.filter(pk__in=ids, status=Photo.Status.ACTIVE)}
    ordered = [photos[i] for i in ids if i in photos]
    if len(ordered) < 2:
        raise ActionError('merge.tooFew')
    taken = hidden_ids() | set(index())
    busy = [p.file_name for p in ordered if p.pk in taken]
    if busy:
        raise ActionError('merge.alreadyMerged', name=', '.join(busy))
    base, sources = ordered[0], ordered[1:]

    base_rgb = _preview_rgb(base)
    # A base entra na rampa: a opacidade dela vale quando houver area nela.
    opacities = default_opacities(len(sources) + 1)
    entries = [{'photo': str(src.pk), 'mask': '', 'opacity': opacity, 'visible': True,
                'align': analyze(base, src, base_rgb)}
               for src, opacity in zip(sources, opacities[1:])]
    return Merge.objects.create(base=base, layers=entries, base_opacity=opacities[0])


def analyze(base, src, base_rgb=None, mask_key=''):
    """Alinhamento de ``src`` sobre ``base`` (``imaging/merge.align``), medido
    nos previews e guardado na resolucao cheia. Com a area ja selecionada, ela
    fica fora da analise: quem alinha e a imagem em volta."""
    if base_rgb is None:
        base_rgb = _preview_rgb(base)
    src_rgb = _preview_rgb(src)
    mask = layers.load_mask(mask_key) if mask_key else None
    if mask is not None and mask.shape != src_rgb.shape[:2]:
        mask = None
    info = mergeimg.align(base_rgb, src_rgb, exclude=mask)
    # Preview -> resolucao cheia, que e como a matriz fica guardada.
    info['matrix'] = mergeimg.scaled(info['matrix'], _size(src_rgb), full_size(src),
                                     _size(base_rgb), full_size(base)).tolist()
    info['mask'] = mask_key if mask is not None else ''
    log.info("Merge %s <- %s: %s inliers=%d err=%s gain=%s", base.file_name,
             src.file_name, mergeimg.describe(info['matrix']), info['inliers'],
             info['error'], info['gain'])
    return info


def _clamp01(value, default):
    try:
        return min(max(float(value), 0.0), 1.0)
    except (TypeError, ValueError):
        return default


def update(merge, items) -> Merge:
    """Area, transparencia e visibilidade das camadas (por foto). Camadas nao
    citadas ficam como estao; a ordem e o alinhamento nao mudam aqui. A foto
    da base tambem pode vir: area e opacidade do objeto dela."""
    by_photo = {str(it.get('photo')): it for it in (items or []) if isinstance(it, dict)}
    base_item = by_photo.get(str(merge.base_id))
    if base_item is not None:
        if 'mask' in base_item:
            key = base_item['mask'] or ''
            if key and layers.load_mask(key) is None:
                raise ActionError('layers.maskNotFound')
            merge.base_mask = key
        if 'opacity' in base_item:
            merge.base_opacity = round(_clamp01(base_item['opacity'], merge.base_opacity), 3)
    out = []
    for entry in merge.layers:
        it = by_photo.get(entry['photo'])
        if it is None:
            out.append(entry)
            continue
        entry = dict(entry)
        if 'mask' in it:
            key = it['mask'] or ''
            if key and layers.load_mask(key) is None:
                raise ActionError('layers.maskNotFound')
            entry['mask'] = key
            if key and (entry.get('align') or {}).get('mask') != key:
                src = Photo.objects.filter(pk=entry['photo']).first()
                if src is not None:
                    entry['align'] = analyze(merge.base, src, mask_key=key)
        if 'opacity' in it:
            entry['opacity'] = round(_clamp01(it['opacity'], entry['opacity']), 3)
        if 'visible' in it:
            entry['visible'] = bool(it['visible'])
        out.append(entry)
    merge.layers = out
    merge.save(update_fields=['layers', 'base_mask', 'base_opacity', 'updated'])
    return merge


def remove_layer(merge, photo_id) -> bool:
    """Tira uma foto da composicao: ela volta a filmstrip. Sem camada
    nenhuma nao sobra merge, e ele e desfeito — devolve ``True`` nesse caso.
    O fundo atras do objeto da base e escolhido de novo sozinho
    (``backdrop_source``), caso viesse da foto removida."""
    layers_left = [e for e in merge.layers if e['photo'] != str(photo_id)]
    if len(layers_left) == len(merge.layers):
        raise ActionError('merge.notALayer')
    if not layers_left:
        dissolve(merge)
        return True
    merge.layers = layers_left
    merge.save(update_fields=['layers', 'updated'])
    log.info("Merge %s: layer %s removed", merge.pk, photo_id)
    return False


def dissolve(merge):
    """Desfaz o merge: a base volta a ser so a foto dela e as fotos das
    camadas voltam a filmstrip. As mascaras ficam em project_data/masks."""
    log.info("Merge %s on %s dissolved", merge.pk, merge.base.file_name)
    merge.delete()


# --------------------------------------------------------------------------- #
# Quem esta em merge
# --------------------------------------------------------------------------- #

def _live():
    """Merges cuja base esta ativa. Base excluida (em deleted/) = merge
    parado: as fotos das camadas voltam a aparecer, e desfazer a exclusao
    traz tudo de volta."""
    return Merge.objects.filter(base__status=Photo.Status.ACTIVE)


def index() -> dict:
    """``{id da base: merge}`` — uma consulta para a lista inteira."""
    return {m.base_id: m for m in _live().select_related('base')}


def for_base(photo):
    return _live().filter(base=photo).first()


def hidden_ids() -> set:
    """Fotos que sao camada de algum merge: fora da filmstrip e do Exportar."""
    return {uuid.UUID(e['photo']) for m in _live() for e in m.layers}


def ready_layers(merge):
    """Camadas que entram na composicao: visiveis e com area."""
    return [e for e in merge.layers if e.get('visible', True) and e.get('mask')]


def _usable(entry, photos):
    src = photos.get(entry['photo'])
    return src is not None and src.status == Photo.Status.ACTIVE \
        and (entry.get('align') or {}).get('ok')


def backdrop_source(merge, photos, mask_key=None):
    """Camada de onde vem o fundo atras do objeto da base: a mais recente
    (o objeto ja andou mais) cuja area, levada a base, nao cai sobre a area
    da base (``mask_key``, ou a gravada). Sem area para comparar, vale a mais
    recente."""
    mask_key = merge.base_mask if mask_key is None else mask_key
    base_mask = layers.load_mask(mask_key) if mask_key else None
    candidates = [e for e in reversed(merge.layers) if _usable(e, photos)]
    if base_mask is None or not candidates:
        return None
    ys, xs = np.nonzero(base_mask > 2)
    if not len(xs):
        return None
    base = photos[str(merge.base_id)]
    bh, bw = base_mask.shape
    margin = CROP_MARGIN
    box = (xs.min() - margin, ys.min() - margin, xs.max() + margin, ys.max() + margin)
    for entry in candidates:
        mask = layers.load_mask(entry['mask']) if entry.get('mask') else None
        if mask is None:
            continue
        mys, mxs = np.nonzero(mask > 2)
        if not len(mxs):
            continue
        src = photos[entry['photo']]
        m = mergeimg.scaled(entry['align']['matrix'], full_size(src), (mask.shape[1], mask.shape[0]),
                            full_size(base), (bw, bh))
        corners = np.array([[mxs.min(), mys.min()], [mxs.max(), mys.max()],
                            [mxs.min(), mys.max()], [mxs.max(), mys.min()]], np.float64)
        pts = corners @ m[:, :2].T + m[:, 2]
        if pts[:, 0].max() < box[0] or pts[:, 0].min() > box[2] \
                or pts[:, 1].max() < box[1] or pts[:, 1].min() > box[3]:
            return entry
    return candidates[0]


def base_fades(merge, photos=None) -> bool:
    """O objeto da base fica mais claro (tem area, opacidade < 1 e de onde
    tirar o fundo)?"""
    if not merge.base_mask or merge.base_opacity >= 0.999:
        return False
    return backdrop_source(merge, photos or _photos(merge)) is not None


def version(merge, photos=None) -> str:
    """Hash de tudo de que a composicao depende; ``''`` quando o merge nao
    muda nada (a foto e so a base). Entra na URL do render e no cache."""
    if merge is None:
        return ''
    photos = photos or _photos(merge)
    ready = ready_layers(merge)
    fades = base_fades(merge, photos)
    if not ready and not fades:
        return ''
    parts = [str(photos[str(merge.base_id)].mtime_ns), _backdrop_version(merge, photos)
             if fades else '']
    for e in ready:
        src = photos.get(e['photo'])
        parts.append(f"{e['photo']}:{src.mtime_ns if src else 0}:{e['mask']}"
                     f":{e.get('opacity', 1.0)}:{align_version(e['align'])}")
    return hashlib.sha1('|'.join(parts).encode()).hexdigest()[:12]


def _backdrop_version(merge, photos):
    entry = backdrop_source(merge, photos)
    if entry is None:
        return ''
    src = photos[entry['photo']]
    return (f"{merge.base_mask}:{merge.base_opacity}:{entry['photo']}:{src.mtime_ns}"
            f":{align_version(entry['align'])}")


def export_name(photo) -> str:
    """Nome da foto de merge em publicar/: ``<base>_merge.<ext>``."""
    name = Path(photo.file_name)
    return f'{name.stem}{EXPORT_SUFFIX}{name.suffix}'


# --------------------------------------------------------------------------- #
# JSON
# --------------------------------------------------------------------------- #

def align_version(align) -> str:
    """Hash curto do alinhamento: entra na URL e no cache do recorte, que
    mudam quando a area nova refaz a analise."""
    raw = json.dumps([align.get('matrix'), align.get('gain')], separators=(',', ':'))
    return hashlib.sha1(raw.encode()).hexdigest()[:10]


def _photo_brief(photo):
    return {
        'id': str(photo.pk),
        'file_name': photo.file_name,
        'width': photo.width,
        'height': photo.height,
        'active': photo.status == Photo.Status.ACTIVE,
        'preview_url': f'/api/photos/{photo.pk}/preview/?v={photo.mtime_ns}',
        'thumbnail_url': f'/api/photos/{photo.pk}/thumbnail/?v={photo.mtime_ns}',
    }


def _photos(merge):
    ids = [merge.base_id] + [e['photo'] for e in merge.layers]
    return {str(p.pk): p for p in Photo.objects.filter(pk__in=ids)}


def merge_json(merge):
    photos = _photos(merge)
    base = photos.get(str(merge.base_id))
    out_layers = []
    for entry in merge.layers:
        photo = photos.get(entry['photo'])
        if photo is None:
            continue
        align = entry.get('align') or {}
        out_layers.append({
            'photo': _photo_brief(photo),
            'mask': entry.get('mask') or None,
            'opacity': entry.get('opacity', 1.0),
            'visible': entry.get('visible', True),
            'align': {**mergeimg.describe(align.get('matrix') or [[1, 0, 0], [0, 1, 0]]),
                      'ok': bool(align.get('ok')), 'inliers': align.get('inliers', 0),
                      'error': align.get('error'), 'gain': align.get('gain')},
            # O frontend soma ``&mask=<chave>`` (a gravada ou a da selecao).
            'cutout_url': (f'/api/merges/{merge.pk}/layers/{photo.pk}.png'
                           f'?v={base.mtime_ns}-{photo.mtime_ns}-{align_version(align)}'),
        })
    return {
        'id': str(merge.pk),
        'base': _photo_brief(base),
        # A area da propria base: o recorte dela e o FUNDO que aparece por
        # tras do objeto, mostrado com opacidade 1 - ``opacity``.
        'base_layer': {'mask': merge.base_mask or None, 'opacity': merge.base_opacity,
                       'cutout_url': (f'/api/merges/{merge.pk}/layers/{base.pk}.png'
                                      f'?v={base.mtime_ns}-{_layers_version(merge, photos)}')},
        'layers': out_layers,
        'export_name': export_name(base),
    }


def _layers_version(merge, photos):
    """O fundo da base depende de qual camada o fornece: das areas e dos
    alinhamentos de todas."""
    raw = '|'.join(f"{e['photo']}:{photos[e['photo']].mtime_ns if e['photo'] in photos else 0}"
                   f":{e.get('mask')}:{align_version(e.get('align') or {})}" for e in merge.layers)
    return hashlib.sha1(raw.encode()).hexdigest()[:10]


# --------------------------------------------------------------------------- #
# Recorte (preview)
# --------------------------------------------------------------------------- #

def _entry(merge, photo_id):
    return next((e for e in merge.layers if e['photo'] == str(photo_id)), None)


def _preview_piece(base, src, entry, mask, out_size):
    """Area da camada (mascara ``mask``) levada ao preview da base."""
    src_rgb = _preview_rgb(src)
    if mask.shape != src_rgb.shape[:2]:
        return None
    align = entry['align']
    matrix = mergeimg.scaled(align['matrix'], full_size(src), _size(src_rgb),
                             full_size(base), out_size)
    return mergeimg.cutout(src_rgb, mask.astype(np.float32) / 255.0, matrix,
                           align.get('gain') or [1, 1, 1], out_size)


def _mask_region(mask, size):
    """A mascara (uint8, do preview) levada a uma imagem de ``size`` (w, h)
    do mesmo quadro, so na caixa da area (com margem): ``(alpha float, (x0,
    y0))``, ou ``None`` se vazia. Ampliar a mascara inteira para 24 MP por
    causa de uma bola custaria 100 MB."""
    ys, xs = np.nonzero(mask > 2)
    if not len(xs):
        return None
    fw, fh = size
    mh, mw = mask.shape
    sx, sy = fw / mw, fh / mh
    margin = CROP_MARGIN if sx > 1.5 else 2
    x0 = max(int(xs.min() * sx) - margin, 0)
    y0 = max(int(ys.min() * sy) - margin, 0)
    x1 = min(int((xs.max() + 1) * sx) + margin, fw)
    y1 = min(int((ys.max() + 1) * sy) + margin, fh)
    # O centro do pixel x da caixa cai em (x0 + x + 0,5) / sx - 0,5 na
    # mascara (idem em y).
    to_mask = np.array([[1 / sx, 0, (x0 + 0.5) / sx - 0.5],
                        [0, 1 / sy, (y0 + 0.5) / sy - 0.5]], np.float64)
    alpha = cv2.warpAffine(mask.astype(np.float32) / 255.0, to_mask, (x1 - x0, y1 - y0),
                           flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP, borderValue=0)
    return alpha, (x0, y0)


def _preview_backdrop(base, src, entry, base_mask):
    """Fundo atras do objeto da base (mascara ``base_mask``), no preview."""
    region = _mask_region(base_mask, (base_mask.shape[1], base_mask.shape[0]))
    if region is None:
        return None
    src_rgb = _preview_rgb(src)
    matrix = mergeimg.scaled(entry['align']['matrix'], full_size(src), _size(src_rgb),
                             full_size(base), (base_mask.shape[1], base_mask.shape[0]))
    return mergeimg.backdrop(src_rgb, region[0], region[1], matrix,
                             entry['align'].get('gain') or [1, 1, 1])


def cutout_png(merge, photo_id, mask_key) -> bytes | None:
    """A area da camada, alinhada sobre o PREVIEW da base: PNG RGBA do tamanho
    dele, transparente fora da area. A transparencia da camada NAO entra
    (o navegador aplica). ``None`` se a camada ou a mascara nao existem.

    Na foto da BASE, o recorte e o fundo que aparece por tras do objeto dela
    (``mergeimg.backdrop``), tirado da camada de ``backdrop_source``.

    O arquivo vai para o cache: a chave carrega tudo de que ele depende."""
    mask = layers.load_mask(mask_key) if mask_key else None
    base = merge.base
    if mask is None:
        return None
    if str(photo_id) == str(base.pk):
        photos = _photos(merge)
        entry = backdrop_source(merge, photos, mask_key)
        if entry is None:
            return None
        src = photos[entry['photo']]
        kind = 'bg'
    else:
        entry = _entry(merge, photo_id)
        src = Photo.objects.filter(pk=photo_id).first()
        if entry is None or src is None:
            return None
        kind = 'fg'
    path = (derivatives.cache_dir('merge')
            / f'{merge.pk}-{kind}-{src.pk}-{base.mtime_ns}-{src.mtime_ns}'
              f'-{align_version(entry["align"])}-{mask_key}.png')
    if path.is_file():
        return path.read_bytes()

    with Image.open(derivatives.preview_path(base)) as img:
        out_size = img.size
    piece = (_preview_backdrop(base, src, entry, mask) if kind == 'bg'
             else _preview_piece(base, src, entry, mask, out_size))
    rgba = np.zeros((out_size[1], out_size[0], 4), np.uint8)
    if piece is not None:
        rgb, a, (x0, y0) = piece
        h, w = a.shape
        rgba[y0:y0 + h, x0:x0 + w, :3] = (rgb + 0.5).astype(np.uint8)
        rgba[y0:y0 + h, x0:x0 + w, 3] = (a * 255.0 + 0.5).astype(np.uint8)
        # Fora da area o RGB nao aparece; zerado, o PNG fica pequeno.
        rgba[rgba[..., 3] == 0, :3] = 0
    buf = io.BytesIO()
    Image.fromarray(rgba, mode='RGBA').save(buf, format='PNG', optimize=True)
    data = buf.getvalue()
    derivatives.write_atomic(path, data)
    return data


# --------------------------------------------------------------------------- #
# A foto do merge (edicao e exportacao)
# --------------------------------------------------------------------------- #

def composite_preview_path(merge, ver):
    """O merge composto no tamanho do preview da base — de onde a "Editada"
    parte. Em cache pela versao: mexer numa camada gera outro arquivo."""
    base = merge.base
    path = derivatives.cache_dir('merge-preview') / f'{base.pk}-{ver}.jpg'
    if path.is_file():
        return path
    photos = _photos(merge)
    with Image.open(derivatives.preview_path(base)) as img:
        canvas = np.array(img.convert('RGB'))
    out_size = _size(canvas)
    if base_fades(merge, photos):
        entry = backdrop_source(merge, photos)
        piece = _preview_backdrop(base, photos[entry['photo']], entry,
                                  layers.load_mask(merge.base_mask))
        if piece is not None:
            mergeimg.paste(canvas, piece, 1.0 - merge.base_opacity)
    for entry in ready_layers(merge):
        src = photos.get(entry['photo'])
        mask = layers.load_mask(entry['mask'])
        if src is None or src.status != Photo.Status.ACTIVE or mask is None:
            continue
        piece = _preview_piece(base, src, entry, mask, out_size)
        if piece is not None:
            mergeimg.paste(canvas, piece, entry.get('opacity', 1.0))
    buf = io.BytesIO()
    Image.fromarray(canvas).save(buf, format='JPEG', quality=derivatives.PREVIEW_QUALITY,
                                 optimize=True)
    derivatives.write_atomic(path, buf.getvalue())
    return path


def edit_source(photo, merge=None):
    """``(preview de onde a edicao parte, versao do merge)``: o merge composto
    se a foto e base de um com alguma area, senao o proprio preview e ``''``.
    ``merge`` evita a consulta quando quem chama ja o tem."""
    merge = merge if merge is not None else for_base(photo)
    ver = version(merge)
    if not ver:
        return derivatives.preview_path(photo), ''
    return composite_preview_path(merge, ver), ver


def _full_piece(src, entry, base, base_size):
    """Area de uma camada na resolucao cheia (``mergeimg.cutout``). A mascara
    vive no preview: so a caixa dela e ampliada, e so esse pedaco da foto de
    origem entra na conta."""
    mask = layers.load_mask(entry['mask'])
    if mask is None:
        return None
    img = load_rgb(raw_path(src))
    region = _mask_region(mask, img.size)
    if region is None:
        return None
    alpha, (x0, y0) = region
    h, w = alpha.shape
    rgb = np.asarray(img.crop((x0, y0, x0 + w, y0 + h)))
    matrix = mergeimg.scaled(entry['align']['matrix'], full_size(src), img.size,
                             full_size(base), base_size)
    return mergeimg.cutout(rgb, alpha, matrix, entry['align'].get('gain') or [1, 1, 1],
                           base_size, offset=(x0, y0))


def _full_backdrop(src, entry, base, base_size, base_mask):
    """Fundo atras do objeto da base, na resolucao cheia."""
    region = _mask_region(base_mask, base_size)
    if region is None:
        return None
    img = load_rgb(raw_path(src))
    matrix = mergeimg.scaled(entry['align']['matrix'], full_size(src), img.size,
                             full_size(base), base_size)
    return mergeimg.backdrop(np.asarray(img), region[0], region[1], matrix,
                             entry['align'].get('gain') or [1, 1, 1])


def render_full(merge):
    """``(RGB uint8 na resolucao cheia, EXIF da base)`` do merge composto —
    o que o Exportar revela no lugar do original da base."""
    photos = _photos(merge)
    base = photos[str(merge.base_id)]
    with Image.open(raw_path(base)) as raw:
        exif = raw.info.get('exif', b'')
    img = load_rgb(raw_path(base))
    canvas = np.array(img)
    size = img.size
    del img
    if base_fades(merge, photos):
        entry = backdrop_source(merge, photos)
        piece = _full_backdrop(photos[entry['photo']], entry, base, size,
                               layers.load_mask(merge.base_mask))
        if piece is not None:
            mergeimg.paste(canvas, piece, 1.0 - merge.base_opacity)
    for entry in ready_layers(merge):
        src = photos.get(entry['photo'])
        if src is None or src.status != Photo.Status.ACTIVE or not raw_path(src).is_file():
            log.warning("Merge %s: layer photo %s is unavailable; skipped.",
                        merge.pk, entry['photo'])
            continue
        piece = _full_piece(src, entry, base, size)
        if piece is not None:
            mergeimg.paste(canvas, piece, entry.get('opacity', 1.0))
    return canvas, exif

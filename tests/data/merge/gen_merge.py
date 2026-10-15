"""Referências do Merge (tests/tst_merge.cpp) geradas pela versão em Python.

Rodar da raiz do repositório (branch legacy ou backend/ ainda presente):

    docker run --rm -v "$PWD/backend":/app:ro -v "$PWD/tests/data":/data -w /app \
        python:3.12-slim sh -c 'pip -q install numpy opencv-python-headless==4.12.0.88 \
        >/dev/null 2>&1; python /data/merge/gen_merge.py'

O par de teste é o sample.jpg e ele mesmo girado/ampliado/deslocado por uma
similaridade conhecida — a mesma chamada de warpAffine se repete no teste.
As funções puras de services/merges.py são importadas com o Django e os
serviços substituídos por módulos vazios.
"""
import json
import sys
import types

import cv2
import numpy as np

sys.path.insert(0, '/app')


def _stub(name, **attrs):
    mod = types.ModuleType(name)
    mod.__dict__.update(attrs)
    sys.modules[name] = mod
    return mod


class _Dummy:
    pass


_stub('photoeditor.models', Merge=_Dummy, Photo=_Dummy)
_stub('photoeditor.imaging.io', load_rgb=None, raw_path=None)
_stub('photoeditor.services.derivatives')
_stub('photoeditor.services.layers')
_stub('photoeditor.services.history', ActionError=Exception)

from photoeditor.imaging import merge as mergeimg  # noqa: E402
from photoeditor.services import merges  # noqa: E402

W, H = 547, 365


def forward():
    """src = base girada 2 graus, ampliada 3% e deslocada (12, -7)."""
    m = cv2.getRotationMatrix2D((W / 2, H / 2), 2.0, 1.03)
    m[0, 2] += 12
    m[1, 2] -= 7
    return m


def make_src(base, lut_num=1, lut_den=1):
    src = cv2.warpAffine(base, forward(), (W, H), flags=cv2.INTER_LINEAR,
                         borderMode=cv2.BORDER_REFLECT)
    if lut_num != lut_den:
        lut = np.array([min(255, i * lut_num // lut_den) for i in range(256)], np.uint8)
        src = cv2.LUT(src, lut)
    return src


def circle_mask(w, h, center, radius):
    mask = np.zeros((h, w), np.uint8)
    cv2.circle(mask, center, radius, 255, -1, cv2.LINE_AA)
    return mask


def checksum(a):
    return float(np.asarray(a, np.float64).sum())


def align_json(info):
    return {k: info[k] for k in ('matrix', 'inliers', 'error', 'ok', 'gain')}


def piece_json(piece):
    if piece is None:
        return None
    rgb, a, (x0, y0) = piece
    return {'origin': [int(x0), int(y0)], 'size': [int(a.shape[1]), int(a.shape[0])],
            'rgb_sum': checksum(rgb), 'alpha_sum': checksum(a)}


def rgba_png(piece, out_size):
    """A montagem do cutout_png (sem o PNG)."""
    rgba = np.zeros((out_size[1], out_size[0], 4), np.uint8)
    if piece is not None:
        rgb, a, (x0, y0) = piece
        h, w = a.shape
        rgba[y0:y0 + h, x0:x0 + w, :3] = (rgb + 0.5).astype(np.uint8)
        rgba[y0:y0 + h, x0:x0 + w, 3] = (a * 255.0 + 0.5).astype(np.uint8)
        rgba[rgba[..., 3] == 0, :3] = 0
    return rgba


def clears(base_mask, mask, m):
    """A geometria do backdrop_source, copiada de services/merges.py."""
    ys, xs = np.nonzero(base_mask > 2)
    margin = merges.CROP_MARGIN
    box = (xs.min() - margin, ys.min() - margin, xs.max() + margin, ys.max() + margin)
    mys, mxs = np.nonzero(mask > 2)
    corners = np.array([[mxs.min(), mys.min()], [mxs.max(), mys.max()],
                        [mxs.min(), mys.max()], [mxs.max(), mys.min()]], np.float64)
    pts = corners @ m[:, :2].T + m[:, 2]
    return bool(pts[:, 0].max() < box[0] or pts[:, 0].min() > box[2]
                or pts[:, 1].max() < box[1] or pts[:, 1].min() > box[3])


def main():
    base = cv2.cvtColor(cv2.imread('/data/sample.jpg'), cv2.COLOR_BGR2RGB)
    src = make_src(base)
    dark = make_src(base, 9, 10)
    ball = circle_mask(W, H, (300, 180), 40)
    out = {'sample_checksum': int(base.astype(np.int64).sum()),
           'src_checksum': int(src.astype(np.int64).sum()),
           'dark_checksum': int(dark.astype(np.int64).sum()),
           'ball_checksum': int(ball.astype(np.int64).sum()),
           'expected_matrix': cv2.invertAffineTransform(forward()).tolist()}

    out['align'] = align_json(mergeimg.align(base, src))
    out['align_dark'] = align_json(mergeimg.align(base, dark))
    out['align_exclude'] = align_json(mergeimg.align(base, src, exclude=ball))
    out['align_flat'] = align_json(mergeimg.align(base, np.full((200, 300, 3), 90, np.uint8)))

    # Funções puras com uma matriz FIXA (independem do SIFT).
    m = np.array([[1.01, -0.02, 5.5], [0.02, 1.01, -3.25]], np.float64)
    gain = [1.1, 0.95, 1.0]
    out['gain_fixed'] = mergeimg.gain(base, src, cv2.invertAffineTransform(forward()))
    out['describe'] = mergeimg.describe(m)
    out['scaled'] = mergeimg.scaled(m, (W, H), (1094, 730), (W, H), (2000, 1333)).tolist()

    alpha = ball.astype(np.float32) / 255.0
    piece = mergeimg.cutout(src, alpha, m, gain, (W, H))
    out['cutout'] = piece_json(piece)
    x0, y0, cw, ch = 250, 130, 120, 110
    piece_off = mergeimg.cutout(np.ascontiguousarray(src[y0:y0 + ch, x0:x0 + cw]),
                                np.ascontiguousarray(alpha[y0:y0 + ch, x0:x0 + cw]), m, gain, (W, H),
                                offset=(x0, y0))
    out['cutout_offset'] = piece_json(piece_off)
    out['cutout_outside'] = piece_json(mergeimg.cutout(src, alpha, np.array(
        [[1, 0, 2000], [0, 1, 0]], np.float64), gain, (W, H)))

    canvas = base.copy()
    mergeimg.paste(canvas, piece, 0.6)
    out['paste_checksum'] = int(canvas.astype(np.int64).sum())
    out['paste_pixel'] = canvas[180, 305].tolist()

    region = merges._mask_region(ball, (W, H))
    out['mask_region'] = {'origin': list(region[1]), 'size': [region[0].shape[1], region[0].shape[0]],
                          'sum': checksum(region[0])}
    region2 = merges._mask_region(ball, (1094, 730))
    out['mask_region_2x'] = {'origin': list(region2[1]), 'size': [region2[0].shape[1], region2[0].shape[0]],
                             'sum': checksum(region2[0])}
    bd = mergeimg.backdrop(src, region[0], region[1], m, gain)
    out['backdrop'] = piece_json(bd)
    canvas = base.copy()
    mergeimg.paste(canvas, bd, 0.75)
    out['backdrop_paste_checksum'] = int(canvas.astype(np.int64).sum())

    rgba = rgba_png(piece, (W, H))
    out['rgba_checksum'] = int(rgba.astype(np.int64).sum())
    out['rgba_opaque'] = int((rgba[..., 3] > 0).sum())

    out['default_opacities'] = {str(n): merges.default_opacities(n) for n in range(0, 7)}
    out['align_version'] = merges.align_version(out['align_flat'])
    out['align_version_fixed'] = merges.align_version({'matrix': m.tolist(), 'gain': gain})
    out['align_version_empty'] = merges.align_version({})
    names = ['IMG_0001.JPG', 'a.b.jpg', 'noext', '.hidden', 'dir/x.jpeg']
    out['export_name'] = {n: merges.export_name(types.SimpleNamespace(file_name=n)) for n in names}

    small = circle_mask(W, H, (100, 100), 20)
    out['clears'] = {
        'near': clears(ball, small, m),
        'far': clears(ball, small, np.array([[1, 0, 0], [0, 1, 0]], np.float64)),
        'onto': clears(ball, small, np.array([[1, 0, 200], [0, 1, 80]], np.float64)),
    }
    with open('/data/merge/expected.json', 'w') as fh:
        json.dump(out, fh, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()

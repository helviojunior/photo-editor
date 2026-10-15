"""Referências do Auto (tests/tst_auto.cpp) geradas pela versão em Python.

Rodar da raiz do repositório (branch legacy ou backend/ ainda presente):

    docker run --rm -v "$PWD/backend":/app:ro -v "$PWD/tests/data":/data -w /app \
        python:3.12-slim sh -c 'pip -q install numpy opencv-python-headless==4.12.0.88 \
        >/dev/null 2>&1; python /data/auto/gen_auto.py'

As variantes são derivadas do sample.jpg com aritmética inteira (LUT por
canal), reproduzida igual no teste em C++.
"""
import json
import sys

import cv2
import numpy as np

sys.path.insert(0, '/app')
from photoeditor.imaging import auto  # noqa: E402


def lut(fn):
    return np.array([min(255, max(0, fn(i))) for i in range(256)], np.uint8)


# nome -> (lut R, lut G, lut B, escala) — a MESMA tabela está no teste.
VARIANTS = {
    'original': (lambda i: i, lambda i: i, lambda i: i, 1),
    'dark': (lambda i: i * 2 // 5, lambda i: i * 2 // 5, lambda i: i * 2 // 5, 1),
    'bright': (lambda i: i * 8 // 5, lambda i: i * 8 // 5, lambda i: i * 8 // 5, 1),
    'warm': (lambda i: i * 6 // 5, lambda i: i, lambda i: i * 3 // 4, 1),
    'flat': (lambda i: 96 + i * 64 // 255, lambda i: 96 + i * 64 // 255, lambda i: 96 + i * 64 // 255, 1),
    'large': (lambda i: i, lambda i: i, lambda i: i, 2),
}


def main():
    rgb = cv2.cvtColor(cv2.imread('/data/sample.jpg'), cv2.COLOR_BGR2RGB)
    out = {'sample_checksum': int(rgb.astype(np.int64).sum()), 'cases': {}}
    for name, (fr, fg, fb, scale) in VARIANTS.items():
        img = np.stack([cv2.LUT(rgb[..., c], lut(f)) for c, f in enumerate((fr, fg, fb))], axis=2)
        if scale != 1:
            img = cv2.resize(img, (img.shape[1] * scale, img.shape[0] * scale),
                             interpolation=cv2.INTER_NEAREST)
        img = np.ascontiguousarray(img)
        h, w = img.shape[:2]
        s = auto.ANALYSIS_SIDE / max(h, w)
        small = cv2.resize(img, (max(int(w * s), 1), max(int(h * s), 1)),
                           interpolation=cv2.INTER_AREA) if s < 1 else img
        x0 = small.astype(np.float32) / 255.0
        f = auto.features(x0)
        w_ = auto.memberships(f)
        params, case = auto.case_params(x0)
        values, case2 = auto.auto_values(img)
        assert case == case2
        out['cases'][name] = {
            'features': {'L': f.L, 'dr': f.dr, 'clip': f.clip, 'subject_bg': f.subject_bg,
                         'whites_rb': None if np.isnan(f.whites_rb) else f.whites_rb},
            'memberships': w_,
            'params': params,
            'illum': [float(v) for v in auto.white_patch_neutral(x0)],
            'hog_pixels': int(auto._hog_boxes(x0).sum()),
            'texture_pixels': int(auto.texture_mask(x0).sum()),
            'subject_pixels': int(auto.subject_region(x0).sum()),
            'case': case,
            'values': values,
        }
    with open('/data/auto/expected.json', 'w') as fh:
        json.dump(out, fh, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()

"""Auto: porte do corretor ``photofix_v2`` do ``../correct-photos`` (TODO 6.3).

O original aplica a correcao direto nos pixels. Aqui ela vira VALORES DOS
SLIDERS — temperature/tint, exposure, contrast e shadows —, que a pessoa
continua podendo ajustar e que o CTRL+Z desfaz. As conversoes sao exatas: o
motor (``develop.render``) reproduz o ganho de branco e o gama que o
``pipeline.estimate`` calcularia, a menos do arredondamento do slider.

Fontes portadas (convertidas de BGR para RGB):

* ``enhance.white_patch_neutral`` — iluminante medido em superficies brancas
  reais da cena (paredes, telhado), nao em gray-world;
* ``subject.texture_mask`` / ``subject.subject_region`` — onde esta o sujeito;
* ``casos.extrair`` / ``pertinencias`` / ``PARAMS_POR_CASO`` — parametros por
  caso de uso (contraluz, superexposta...), com pertinencia suave;
* ``pipeline.estimate`` — exposicao ancorada nos tons NEUTROS do sujeito.

O YOLO do original e opcional e pesado; como no ``pipeline.estimate``, a
mascara de sujeito aqui e a de textura.
"""
from __future__ import annotations

from dataclasses import dataclass

import cv2
import numpy as np

from photoeditor.imaging import develop
from photoeditor.imaging.develop import EPS, luma

ANALYSIS_SIDE = 750


# --------------------------------------------------------------------------- #
# enhance.white_patch_neutral
# --------------------------------------------------------------------------- #

def white_patch_neutral(img, lum_lo=0.75, lum_hi=0.97, sat_max=0.25):
    """Iluminante estimado nos pixels claros E neutros da cena.

    Limiar ABSOLUTO de saturacao: com quantil, numa cena toda azulada os
    "menos saturados" eram justamente os que menos revelavam a dominante.
    """
    y = luma(img)
    mx, mn = img.max(axis=2), img.min(axis=2)
    sat = (mx - mn) / np.maximum(mx, EPS)
    band = (y > lum_lo) & (y < lum_hi) & (mx < 0.98)
    if band.sum() < y.size * 0.002:
        band = (y > np.quantile(y, 0.80)) & (y < lum_hi) & (mx < 0.98)
    if band.sum() < 200:
        return np.ones(3, np.float32)
    sel = band & (sat <= sat_max)
    if sel.sum() < 200:
        sel = band
    return np.maximum(img[sel].mean(axis=0), EPS)


# --------------------------------------------------------------------------- #
# subject.py
# --------------------------------------------------------------------------- #

def texture_mask(img, clip_hi=0.97):
    """Regioes com detalhe e sem clipping: o fundo estourado do ginasio e liso."""
    y = luma(img)
    g8 = np.clip(y * 255, 0, 255).astype(np.uint8)
    gx = cv2.Sobel(g8, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(g8, cv2.CV_32F, 0, 1, ksize=3)
    energy = cv2.blur(cv2.magnitude(gx, gy), (31, 31))
    mask = (energy > float(np.quantile(energy, 0.70))) & (y < clip_hi)
    if mask.sum() < y.size * 0.01:
        mask = y < clip_hi
    return mask


_HOG = None


def _hog_boxes(img):
    global _HOG
    if _HOG is None:
        _HOG = cv2.HOGDescriptor()
        _HOG.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
    u8 = np.clip(img * 255, 0, 255).astype(np.uint8)
    mask = np.zeros(img.shape[:2], bool)
    try:
        rects, _ = _HOG.detectMultiScale(u8, winStride=(8, 8), padding=(8, 8), scale=1.06)
    except cv2.error:
        return mask
    for (x0, y0, w, h) in rects:
        mask[max(y0, 0):y0 + h, max(x0, 0):x0 + w] = True
    return mask


def subject_region(img):
    """Regiao do sujeito SEM vies de gradiente (cor + detector de pessoas)."""
    mx, mn = img.max(axis=2), img.min(axis=2)
    sat = cv2.blur((mx - mn) / np.maximum(mx, EPS), (21, 21))
    region = sat > max(float(np.quantile(sat, 0.75)), 0.12)
    boxes = _hog_boxes(img)
    if boxes.sum() > img.shape[0] * img.shape[1] * 0.01:
        region = region | boxes
    if region.sum() < img.shape[0] * img.shape[1] * 0.01:
        region = np.ones(img.shape[:2], bool)
    return region


# --------------------------------------------------------------------------- #
# casos.py — parametros por caso de uso, com pertinencia suave
# --------------------------------------------------------------------------- #

@dataclass
class Features:
    L: float
    dr: float
    clip: float
    subject_bg: float      # stops entre sujeito e fundo (positivo = fundo claro)
    whites_rb: float       # R/B nas superficies que deveriam ser brancas


def _whites_rb(img):
    y = luma(img)
    mx, mn = img.max(axis=2), img.min(axis=2)
    sat = (mx - mn) / np.maximum(mx, EPS)
    sel = (y > 0.6) & (y < 0.97) & (sat < 0.20) & (mx < 0.98)
    if sel.sum() < 200:
        return float('nan')
    px = img[sel]
    return float(px[:, 0].mean() / max(px[:, 2].mean(), EPS))


def features(img) -> Features:
    y = luma(img)
    reg = subject_region(img)
    y_sub = y[reg] if reg.sum() > 500 else y.ravel()
    y_bg = y[~reg] if (~reg).sum() > 500 else y.ravel()
    a = max(float(np.quantile(y_sub, 0.5)), 0.005)
    b = max(float(np.quantile(y_bg, 0.5)), 0.005)
    return Features(
        L=float(cv2.cvtColor(img.astype(np.float32), cv2.COLOR_RGB2LAB)[:, :, 0].mean()),
        dr=float(np.quantile(y, 0.95) - np.quantile(y, 0.05)),
        clip=float((y >= 0.99).mean()),
        subject_bg=float(np.log2(b / a)),
        whites_rb=_whites_rb(img),
    )


# Busca em grade do ../correct-photos (benchmark/grid_casos.py): 9072
# avaliacoes. wb_strength=1,0 vence nos seis casos; shadow so paga em
# contraluz e subexposta; superexposta quer alvo BAIXO.
PARAMS_BY_CASE = {
    'backlit':       dict(target=0.46, shadow=0.70, wb_strength=1.0, contrast=0.12),
    'overexposed':   dict(target=0.38, shadow=0.00, wb_strength=1.0, contrast=0.12),
    'underexposed':  dict(target=0.46, shadow=0.70, wb_strength=1.0, contrast=0.28),
    'color_cast':    dict(target=0.46, shadow=0.00, wb_strength=1.0, contrast=0.12),
    'low_contrast':  dict(target=0.46, shadow=0.00, wb_strength=1.0, contrast=0.12),
    'normal':        dict(target=0.46, shadow=0.00, wb_strength=1.0, contrast=0.28),
}

# Limiares calibrados nos percentis do acervo de 2026-09-22 (556 fotos) e
# largura da transicao de cada feature, na unidade dela.
THRESHOLDS = dict(backlit_stops=0.360, over_clip=0.068, over_L=77.996,
                  under_L=57.985, cast_rb=0.880, low_dr=0.589)
WIDTHS = dict(subject_bg=0.30, clip=0.05, L=12.0, whites_rb=0.08, dr=0.12)


def _ramp(x, threshold, width):
    if np.isnan(x):
        return 0.0
    return float(np.clip((x - threshold) / max(width, EPS) + 0.5, 0.0, 1.0))


def memberships(f: Features) -> dict:
    """Pertinencia a cada caso, somando 1. Sem fronteira: duas fotos da mesma
    rajada recebem tratamentos parecidos por construcao."""
    t, wd = THRESHOLDS, WIDTHS
    w = {
        'backlit': _ramp(f.subject_bg, t['backlit_stops'], wd['subject_bg']),
        'overexposed': max(_ramp(f.clip, t['over_clip'], wd['clip']),
                           _ramp(f.L, t['over_L'], wd['L'])),
        'underexposed': 1.0 - _ramp(f.L, t['under_L'], wd['L']),
        'color_cast': 1.0 - _ramp(f.whites_rb, t['cast_rb'], wd['whites_rb']),
        'low_contrast': 1.0 - _ramp(f.dr, t['low_dr'], wd['dr']),
    }
    w['normal'] = max(0.0, 1.0 - max(w.values()))
    total = sum(w.values())
    return {k: v / total for k, v in w.items()} if total > EPS else {'normal': 1.0}


def case_params(img) -> tuple[dict, str]:
    w = memberships(features(img))
    keys = ('target', 'shadow', 'wb_strength', 'contrast')
    params = {k: sum(w[c] * PARAMS_BY_CASE[c][k] for c in w) for k in keys}
    return params, max(w, key=w.get)


# --------------------------------------------------------------------------- #
# pipeline.estimate -> valores dos sliders
# --------------------------------------------------------------------------- #

def auto_values(img_u8: np.ndarray) -> tuple[dict, str]:
    """(ajustes de luz e cor, caso dominante) para uma imagem RGB uint8."""
    h, w = img_u8.shape[:2]
    s = ANALYSIS_SIDE / max(h, w)
    small = cv2.resize(img_u8, (max(int(w * s), 1), max(int(h * s), 1)),
                       interpolation=cv2.INTER_AREA) if s < 1 else img_u8
    x0 = small.astype(np.float32) / 255.0

    params, case = case_params(x0)

    # (a) balanco de branco ancorado em superficies brancas reais
    illum = white_patch_neutral(x0)
    gain = illum.mean() / illum
    gain = 1.0 + (gain - 1.0) * params['wb_strength']
    temperature, tint = develop.gains_to_wb(gain)
    wb = develop.normalize({'temperature': temperature, 'tint': tint})
    # A exposicao e medida na imagem COM o branco que o motor vai aplicar.
    x = np.clip(x0 * develop.wb_gains(wb['temperature'], wb['tint']), 0, 1)

    # (b) exposicao nos tons NEUTROS do sujeito (pele, branco do uniforme): uma
    #     camisa roxa e escura por refletancia, nao por falta de luz.
    y = luma(x)
    mask = texture_mask(x)
    mx, mn = x.max(axis=2), x.min(axis=2)
    neutral = mask & ((mx - mn) / np.maximum(mx, EPS) < 0.60)
    ya = y[neutral] if neutral.sum() > 500 else (y[mask] if mask.sum() > 500 else y.ravel())
    anchor = min(max(float(np.quantile(ya, 0.35)), 0.01), 0.99)
    target = min(max(params['target'], 0.01), 0.99)
    gamma = float(np.clip(np.log(target) / np.log(anchor), 0.25, 4.0))   # max_ev = 2

    values = develop.normalize({
        **wb,
        'exposure': develop.gamma_to_exposure(gamma),
        'contrast': params['contrast'] * 100,
        'shadows': params['shadow'] * 100,
    })
    light_and_wb = ('exposure', 'contrast', 'highlights', 'shadows', 'whites',
                    'blacks', 'temperature', 'tint')
    return {k: values[k] for k in light_and_wb}, case

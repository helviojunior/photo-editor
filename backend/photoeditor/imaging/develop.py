"""Motor de revelacao: original + ajustes -> foto editada.

E o MESMO motor para o preview do editor e para a exportacao (TODO 6.2): o que
se ve e o que se exporta. Os ajustes nunca tocam o arquivo original; sao
numeros no banco (``Adjustment``) aplicados sobre os pixels a cada render.

Convencao: RGB float32 em [0, 1]. A ordem das etapas segue o ``photofix_v2``
do ``../correct-photos`` (``pipeline.apply_full``):

    1. balanco de branco (temperature/tint)  -> ganho por canal
    2. curva de tom na LUMINANCIA            -> exposure, contrast,
       (ganho que preserva matiz)               highlights, whites, blacks
    3. realce local de sombras               -> shadows
    4. cor                                   -> vibrance, saturation

A curva de tom e resolvida numa rampa de 1024 pontos e aplicada por
interpolacao: o custo nao depende de quantos controles estao ligados.
"""
from __future__ import annotations

import hashlib
import json
import math

import cv2
import numpy as np

# Sobe quando o resultado de um mesmo conjunto de ajustes muda: invalida os
# renders em cache e forca a reexportacao.
ENGINE_VERSION = 1

EPS = 1e-6

# name -> (min, max, step, group). A ordem e a ordem do painel.
SLIDERS = {
    'exposure': (-3.0, 3.0, 0.05, 'light'),
    'contrast': (-100, 100, 1, 'light'),
    'highlights': (-100, 100, 1, 'light'),
    'shadows': (-100, 100, 1, 'light'),
    'whites': (-100, 100, 1, 'light'),
    'blacks': (-100, 100, 1, 'light'),
    'temperature': (-100, 100, 1, 'color'),
    'tint': (-100, 100, 1, 'color'),
    'vibrance': (-100, 100, 1, 'color'),
    'saturation': (-100, 100, 1, 'color'),
}
NEUTRAL = {name: 0 for name in SLIDERS}

# Presets (TODO 6.5): DESLOCAMENTOS somados aos ajustes da foto, e nao valores
# absolutos. Assim o preset e o "look" e convive com o Auto e com o ajuste fino
# da propria foto — aplicar "Quente" numa foto ja corrigida nao desfaz a
# correcao, so esquenta. Inspirados nos perfis mais comuns do Lightroom/VSCO.
PRESETS = {
    'vivid': {'contrast': 20, 'vibrance': 30, 'saturation': 10},
    'soft': {'contrast': -20, 'highlights': -25, 'shadows': 20, 'saturation': -10},
    'warm': {'temperature': 20, 'vibrance': 10},
    'cool': {'temperature': -20, 'tint': -5},
    'bw': {'saturation': -100, 'contrast': 20},
    'matte': {'blacks': 30, 'contrast': -15, 'saturation': -20, 'temperature': 8},
    'vintage': {'temperature': 15, 'tint': 5, 'saturation': -25, 'blacks': 20,
                'whites': -15, 'contrast': -10},
}


# --------------------------------------------------------------------------- #
# Valores
# --------------------------------------------------------------------------- #

def _clamp(name, value):
    lo, hi, step, _ = SLIDERS[name]
    value = min(max(float(value), lo), hi)
    value = round(round(value / step) * step, 2)
    return int(value) if float(step).is_integer() else value


def normalize(values) -> dict:
    """Todos os controles, dentro do limite e no passo do slider."""
    out = dict(NEUTRAL)
    for name, value in (values or {}).items():
        if name in SLIDERS:
            try:
                out[name] = _clamp(name, value)
            except (TypeError, ValueError):
                pass
    return out


def normalize_preset(preset) -> str:
    return preset if preset in PRESETS else ''


def effective(values, preset='') -> dict:
    """Ajustes da foto + deslocamentos do preset, limitados aos sliders."""
    out = normalize(values)
    for name, delta in PRESETS.get(preset, {}).items():
        out[name] = _clamp(name, out[name] + delta)
    return out


def is_neutral(values, preset='') -> bool:
    return all(v == 0 for v in effective(values, preset).values())


def settings_hash(values, preset='') -> str:
    """Identidade do resultado: mesmos ajustes + mesma versao = mesma imagem."""
    payload = json.dumps([ENGINE_VERSION, effective(values, preset)], sort_keys=True)
    return hashlib.sha1(payload.encode()).hexdigest()[:16]


# --------------------------------------------------------------------------- #
# Conversoes usadas tambem pelo Auto
# --------------------------------------------------------------------------- #

# Temperatura e tint sao dois eixos no espaco log dos ganhos RGB, com media
# geometrica 1 (o brilho medio nao muda):
#   temperature: R sobe, B desce     tint: G desce, R e B sobem (magenta)
WB_SCALE = 0.005        # 100 no slider = ganho e^0,5 ~ 1,65 no canal


def wb_gains(temperature, tint) -> np.ndarray:
    t, m = temperature * WB_SCALE, tint * WB_SCALE
    return np.exp(np.array([t + m / 2, -m, -t + m / 2], np.float32))


def gains_to_wb(gains) -> tuple[float, float]:
    """Inverso exato de ``wb_gains`` para qualquer ganho RGB positivo."""
    lg = np.log(np.maximum(np.asarray(gains, np.float64), EPS))
    lg -= lg.mean()
    return (lg[0] - lg[2]) / 2 / WB_SCALE, -lg[1] / WB_SCALE


# Exposicao = gama sobre a luminancia, que fixa 0 -> 0 e 1 -> 1: o branco
# continua branco e so os tons medios andam (a licao do photofix_v2 — o modelo
# de resposta da camera transformava o branco estourado em cinza sujo). A
# escala faz 1 no slider mover o tom medio (0,42) em ~1 stop.
MID_TONE = 0.42
_K = -math.log(MID_TONE)


def exposure_gamma(exposure) -> float:
    return 2.0 ** (-exposure / _K)


def gamma_to_exposure(gamma) -> float:
    return -_K * math.log2(gamma)


def luma(x: np.ndarray) -> np.ndarray:
    """Luminancia Rec.709 de RGB float."""
    return 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]


def s_curve(y, amount):
    """Curva S ancorada em 0,5 (identidade <-> smoothstep). Negativa achata."""
    if amount == 0:
        return y
    smooth = y * y * (3.0 - 2.0 * y)
    return np.clip((1.0 - amount) * y + amount * smooth, 0, 1)


# --------------------------------------------------------------------------- #
# Curva de tom
# --------------------------------------------------------------------------- #

_RAMP = np.linspace(0.0, 1.0, 1024, dtype=np.float32)


def tone_ramp(v: dict) -> np.ndarray:
    y = np.power(_RAMP, exposure_gamma(v['exposure']))
    y = s_curve(y, v['contrast'] / 100.0)

    # highlights: sino em [0,5; 1] com pico em 0,75 e zero no branco — mexe
    # nas altas luzes sem mover o ponto branco. Amplitude pequena o bastante
    # para a curva continuar monotona.
    if v['highlights']:
        bump = np.where(y > 0.5, 16.0 * (y - 0.5) * (1.0 - y), 0.0)
        y = np.clip(y + 0.12 * (v['highlights'] / 100.0) * bump, 0, 1)

    # whites/blacks = levels: positivo em whites corta o branco mais cedo,
    # negativo baixa o branco de saida; em blacks, negativo esmaga o preto e
    # positivo levanta (o "matte").
    w, b = v['whites'] / 100.0, v['blacks'] / 100.0
    in_white = 1.0 - 0.25 * max(w, 0.0)
    out_white = 1.0 + 0.25 * min(w, 0.0)
    in_black = 0.15 * max(-b, 0.0)
    out_black = 0.15 * max(b, 0.0)
    if w or b:
        y = np.clip((y - in_black) / (in_white - in_black), 0, 1)
        y = out_black + (out_white - out_black) * y
    return y.astype(np.float32)


# --------------------------------------------------------------------------- #
# Sombras (porte de enhance.shadow_mask / shadow_lift)
# --------------------------------------------------------------------------- #

SHADOW_ANALYSIS_SIDE = 750


def _guided(p, g, r, eps=1e-3):
    """Guided filter (He, Sun & Tang, 2010): suaviza sem halo na silhueta."""
    k = (r, r)
    mp, mg = cv2.boxFilter(p, -1, k), cv2.boxFilter(g, -1, k)
    cov = cv2.boxFilter(p * g, -1, k) - mp * mg
    var = cv2.boxFilter(g * g, -1, k) - mg * mg
    a = cov / (var + eps)
    b = mp - a * mg
    return cv2.boxFilter(a, -1, k) * g + cv2.boxFilter(b, -1, k)


def shadow_mask(x, knee=0.45, floor=0.12, sat_sup=0.7):
    """Peso do realce: pico nas sombras medias, zero no preto e onde ha COR.

    ``floor`` protege o preto profundo (short preto nao vira cinza); ``sat_sup``
    separa sombra (escura e dessaturada) de objeto escuro colorido (camisa
    roxa), que nao deve clarear. Ver ``../correct-photos`` para as medidas.
    """
    y = luma(x).astype(np.float32)
    base = np.clip(_guided(y, y, r=max(max(y.shape) // 16, 3)), 0, 1)
    m = np.clip((knee - base) / knee, 0, 1) ** 1.5 * np.clip(base / floor, 0, 1)
    if sat_sup > 0:
        mx, mn = x.max(axis=2), x.min(axis=2)
        sat = cv2.blur(((mx - mn) / np.maximum(mx, EPS)).astype(np.float32), (15, 15))
        m = m * np.clip(1.0 - sat / sat_sup, 0, 1)
    return m.astype(np.float32)


def _shadow_mask_full(x):
    """Mascara calculada numa miniatura e ampliada: sombra e estrutura de baixa
    frequencia, e a ampliada correlaciona 0,99 com a de resolucao plena."""
    h, w = x.shape[:2]
    s = SHADOW_ANALYSIS_SIDE / max(h, w)
    small = cv2.resize(x, (max(int(w * s), 1), max(int(h * s), 1)),
                       interpolation=cv2.INTER_AREA) if s < 1 else x
    m = shadow_mask(small)
    return cv2.resize(m, (w, h), interpolation=cv2.INTER_LINEAR) if s < 1 else m


# --------------------------------------------------------------------------- #
# Render
# --------------------------------------------------------------------------- #

def render(img: np.ndarray, values, preset='') -> np.ndarray:
    """Aplica os ajustes numa imagem RGB uint8 e devolve RGB uint8."""
    v = effective(values, preset)
    if all(val == 0 for val in v.values()):
        return img.copy()

    x = img.astype(np.float32) / 255.0

    # 1. balanco de branco
    if v['temperature'] or v['tint']:
        x *= wb_gains(v['temperature'], v['tint'])[None, None, :]
        np.clip(x, 0, 1, out=x)

    # 2. tom na luminancia, com ganho multiplicativo (preserva matiz)
    tone_keys = ('exposure', 'contrast', 'highlights', 'whites', 'blacks')
    if any(v[k] for k in tone_keys):
        y = luma(x)
        y_new = np.interp(y, _RAMP, tone_ramp(v)).astype(np.float32)
        x *= (y_new / np.maximum(y, EPS))[..., None]
        np.clip(x, 0, 1, out=x)

    # 3. sombras locais. O ganho e MULTIPLICATIVO — o unico que preserva a
    #    saturacao; negativo escurece as sombras pela mesma mascara.
    if v['shadows']:
        m = _shadow_mask_full(x)
        y = np.maximum(luma(x), EPS)
        y_new = np.power(y, 1.0 - (v['shadows'] / 100.0) * m)
        x *= (y_new / y)[..., None]
        np.clip(x, 0, 1, out=x)

    # 4. cor: vibrance pesa mais onde a cor e fraca (pele e ceu antes do
    #    uniforme ja saturado); saturation e uniforme. -100 = preto e branco.
    if v['vibrance'] or v['saturation']:
        y = luma(x)[..., None]
        factor = 1.0 + v['saturation'] / 100.0
        if v['vibrance']:
            mx, mn = x.max(axis=2), x.min(axis=2)
            sat = (mx - mn) / np.maximum(mx, EPS)
            factor = factor * (1.0 + (v['vibrance'] / 100.0) * (1.0 - sat))[..., None]
        x = y + (x - y) * factor
        np.clip(x, 0, 1, out=x)

    return (x * 255.0 + 0.5).astype(np.uint8)


def describe() -> dict:
    """O que o frontend precisa para montar o painel."""
    return {
        'engine': ENGINE_VERSION,
        'sliders': [{'name': n, 'min': lo, 'max': hi, 'step': st, 'group': g}
                    for n, (lo, hi, st, g) in SLIDERS.items()],
        'presets': [{'id': k, 'values': v} for k, v in PRESETS.items()],
    }

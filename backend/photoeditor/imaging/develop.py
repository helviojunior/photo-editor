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


def is_neutral(values, preset='', layers=None) -> bool:
    return (all(v == 0 for v in effective(values, preset).values())
            and all(is_neutral(layer['values'], layer['preset'])
                    for layer in layers or ()))


# --------------------------------------------------------------------------- #
# Camadas
#
# Uma camada e uma area da foto (mascara) com os PROPRIOS ajustes. Os ajustes
# da foto (``values``/``preset``) passam a valer so para o restante: cada
# camada e revelada a partir do original, nao por cima do restante, e a
# mascara decide quanto de cada uma aparece em cada pixel. Escurecer o fundo
# nao escurece a pessoa recortada.
#
# A mascara mora num PNG em ``project_data/masks`` com o nome igual ao hash do
# conteudo (``services/layers.py``); aqui so circula a chave. Mascara nunca
# muda de conteudo, entao a chave entra no hash do render como um numero.
# --------------------------------------------------------------------------- #

MAX_LAYERS = 8
_MASK_KEY_CHARS = set('0123456789abcdef')
MASK_KEY_LEN = 20


def is_mask_key(key) -> bool:
    return (isinstance(key, str) and len(key) == MASK_KEY_LEN
            and set(key) <= _MASK_KEY_CHARS)


def normalize_layers(layers) -> list:
    """Camadas validas, na ordem (a de baixo primeiro).

    Deterministico — normalizar duas vezes da o mesmo resultado, que e o que
    o ``apply_state`` compara para saber se algo mudou. O ``id`` vem do
    frontend; sem ele (ou repetido), sai um derivado da posicao e da mascara.
    """
    out, seen = [], set()
    for i, layer in enumerate(layers if isinstance(layers, list) else ()):
        if not isinstance(layer, dict) or not is_mask_key(layer.get('mask')):
            continue
        lid = str(layer.get('id') or '')
        if not (0 < len(lid) <= 32 and lid.isalnum()) or lid in seen:
            lid = f"{layer['mask'][:8]}{i}"
        seen.add(lid)
        out.append({'id': lid, 'mask': layer['mask'],
                    'values': normalize(layer.get('values')),
                    'preset': normalize_preset(layer.get('preset'))})
        if len(out) == MAX_LAYERS:
            break
    return out


# --------------------------------------------------------------------------- #
# Crop
#
# Sempre na PROPORCAO da foto: o recorte e a propria foto reduzida por `scale`
# (0,1..1), centrada em (cx, cy) — fracoes da largura e da altura — e girada
# `angle` graus (positivo = horario, como o `rotate()` do CSS). A foto fica
# parada; quem gira e o quadro. A saida e o que esta sob ele, com o conteudo
# em pe: 90 graus so troca retrato/paisagem (ver `apply_crop`).
#
# O quadro girado tem de caber inteiro na foto. Ele cabe se, e so se, a caixa
# que o envolve cabe (os extremos da caixa sao os cantos do quadro), entao a
# restricao e fechada: `crop_max_scale` limita o tamanho pelo angulo e o
# centro fica a meia caixa das bordas. Girar mais encolhe o quadro sozinho.
# --------------------------------------------------------------------------- #

CROP_IDENTITY = {'scale': 1.0, 'cx': 0.5, 'cy': 0.5, 'angle': 0.0}
CROP_MIN_SCALE = 0.1
# ±90°: a 90° o quadro fica "deitado" sobre a foto e o recorte sai na
# orientacao trocada (retrato numa foto paisagem), com o conteudo em pe.
CROP_MAX_ANGLE = 90.0


def crop_max_scale(angle, aspect) -> float:
    """Maior escala em que o quadro girado ainda cabe (aspect = altura/largura)."""
    c, s = abs(math.cos(math.radians(angle))), abs(math.sin(math.radians(angle)))
    return min(1.0, 1.0 / (c + aspect * s), 1.0 / (s / aspect + c))


def _crop_extents(crop, aspect):
    """Meia caixa envolvente do quadro, em fracao da largura e da altura."""
    c = abs(math.cos(math.radians(crop['angle'])))
    s = abs(math.sin(math.radians(crop['angle'])))
    return (crop['scale'] / 2 * (c + aspect * s),
            crop['scale'] / 2 * (s / aspect + c))


def normalize_crop(crop, aspect) -> dict:
    """Crop valido para uma foto de proporcao ``aspect`` (altura/largura)."""
    crop = crop or {}

    def num(key):
        try:
            value = float(crop.get(key, CROP_IDENTITY[key]))
        except (TypeError, ValueError):
            return CROP_IDENTITY[key]
        return value if math.isfinite(value) else CROP_IDENTITY[key]

    aspect = aspect if aspect and aspect > 0 else 1.0
    angle = round(min(max(num('angle'), -CROP_MAX_ANGLE), CROP_MAX_ANGLE), 1)
    scale = min(max(num('scale'), CROP_MIN_SCALE), crop_max_scale(angle, aspect))
    out = {'scale': scale, 'angle': angle}
    ex, ey = _crop_extents(out, aspect)
    out['cx'] = min(max(num('cx'), ex), 1.0 - ex)
    out['cy'] = min(max(num('cy'), ey), 1.0 - ey)
    out = {k: round(out[k], 4) if k != 'angle' else out[k] for k in CROP_IDENTITY}
    return dict(CROP_IDENTITY) if is_crop_identity(out) else out


def is_crop_identity(crop) -> bool:
    crop = crop or CROP_IDENTITY
    return (crop['angle'] == 0 and crop['scale'] >= 0.9999
            and abs(crop['cx'] - 0.5) < 1e-4 and abs(crop['cy'] - 0.5) < 1e-4)


def split_angle(angle) -> tuple[int, float]:
    """(quartos de volta, resto em [-45, 45)) de um angulo do quadro."""
    quarters = int(math.floor((angle + 45.0) / 90.0))
    return quarters, angle - 90.0 * quarters


def apply_crop(img: np.ndarray, crop) -> np.ndarray:
    """Recorta o que esta sob o quadro. ``crop`` ja normalizado.

    O giro do quadro NAO gira a foto: os quartos de volta (90 graus) so trocam
    a orientacao do recorte — um quadro a 90 graus numa foto paisagem sai em
    retrato, com o conteudo em pe. So o resto (ate 45 graus) endireita o
    conteudo, como o Straighten do Lightroom.
    """
    if not crop or is_crop_identity(crop):
        return img
    h, w = img.shape[:2]
    quarters, rest = split_angle(crop['angle'])
    fw, fh = crop['scale'] * w, crop['scale'] * h        # quadro, nos eixos dele
    if quarters % 2:                                     # deitado: troca os lados
        fw, fh = fh, fw
    ow, oh = max(int(round(fw)), 1), max(int(round(fh)), 1)
    t = math.radians(rest)
    cos, sin = math.cos(t), math.sin(t)
    cx, cy, ocx, ocy = crop['cx'] * w, crop['cy'] * h, ow / 2.0, oh / 2.0
    # Saida -> origem: centro do quadro + rotacao do deslocamento. Com
    # WARP_INVERSE_MAP o OpenCV usa a matriz nesse sentido, sem inverter.
    m = np.array([[cos, -sin, cx - cos * ocx + sin * ocy],
                  [sin, cos, cy - sin * ocx - cos * ocy]], np.float32)
    return cv2.warpAffine(img, m, (ow, oh),
                          flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP,
                          borderMode=cv2.BORDER_REPLICATE)


def settings_hash(values, preset='', crop=None, layers=None) -> str:
    """Identidade do resultado: mesmos ajustes + mesma versao = mesma imagem."""
    parts = [ENGINE_VERSION, effective(values, preset)]
    # Sem crop, o hash e o mesmo de antes de o crop existir: nada ja
    # renderizado ou exportado fica "sujo" so por causa desta versao. O mesmo
    # vale para as camadas.
    if crop and not is_crop_identity(crop):
        parts.append(crop)
    if layers:
        parts.append({'layers': [[layer['mask'], effective(layer['values'], layer['preset'])]
                                 for layer in layers]})
    payload = json.dumps(parts, sort_keys=True)
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


def render_layers(img: np.ndarray, values, preset='', layers=()) -> np.ndarray:
    """Restante com ``values``/``preset``; cada camada ``(values, preset,
    alpha)`` revelada do mesmo ``img`` e misturada por cima pela ``alpha``
    (float32 em [0, 1], do tamanho de ``img``). Sem camadas = ``render``."""
    out = render(img, values, preset)
    if not layers:
        return out
    acc = out.astype(np.float32)
    for layer_values, layer_preset, alpha in layers:
        top = render(img, layer_values, layer_preset).astype(np.float32)
        acc += (top - acc) * alpha[..., None]
    return (acc + 0.5).astype(np.uint8)


def describe() -> dict:
    """O que o frontend precisa para montar o painel."""
    return {
        'engine': ENGINE_VERSION,
        'sliders': [{'name': n, 'min': lo, 'max': hi, 'step': st, 'group': g}
                    for n, (lo, hi, st, g) in SLIDERS.items()],
        'presets': [{'id': k, 'values': v} for k, v in PRESETS.items()],
        'crop': {'min_scale': CROP_MIN_SCALE, 'max_angle': CROP_MAX_ANGLE},
    }

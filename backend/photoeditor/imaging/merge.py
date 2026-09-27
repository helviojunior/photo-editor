"""Merge: areas de varias fotos coladas, alinhadas, sobre uma foto base.

O caso de uso e a trajetoria da bola: a sequencia de cliques da mesma jogada,
a primeira foto como base e, de cada uma das seguintes, so a bola — cada uma
numa camada com a sua transparencia.

A camera esta na mao: entre um clique e outro o quadro anda alguns pixels,
gira uma fracao de grau e o zoom respira. Colar a area na mesma posicao do
pixel desalinharia o fundo em volta dela. Por isso, antes de extrair, cada
foto e ALINHADA a base pelo fundo:

* **Geometria** — pontos SIFT das duas fotos, pareados (teste da razao de
  Lowe) e filtrados por RANSAC numa transformacao de SIMILARIDADE (escala,
  rotacao e translacao). Quem se mexeu (jogadores, a bola) sai como outlier;
  o que sobra e o ginasio parado. Medido na sequencia de teste (2026-09-27):
  ~2.200 pontos de fundo por par e erro mediano de 0,2 px no preview de
  1600 px. Uma homografia nao reduziu o erro — a similaridade, com 4 graus de
  liberdade, e a mais estavel.
* **Luz** — com as fotos ja alinhadas, os pixels onde as duas coincidem sao
  fundo; a razao das medias deles, por canal, e o ganho que iguala exposicao e
  balanco de branco da area colada ao da base.

As matrizes guardadas estao em pixels da foto INTEIRA (na orientacao de
exibicao) de cada lado: servem ao preview e a resolucao cheia, trocando so a
escala (``scaled``).
"""
from __future__ import annotations

import math

import cv2
import numpy as np

# Pontos por foto: o ginasio tem textura de sobra (telhas, faixas), e 6.000
# pontos no preview custam ~0,2 s por par.
FEATURES = 6000
LOWE_RATIO = 0.75
RANSAC_PX = 2.0
# Menos pontos de fundo que isso = as fotos nao sao da mesma cena (ou quase
# tudo mudou); a camada fica sem alinhamento e o frontend avisa.
MIN_INLIERS = 30
GAIN_RANGE = (0.7, 1.4)
# Pixel e "fundo" quando as fotos alinhadas diferem menos que isso (0..255).
STATIC_DIFF = 12


def _gray(rgb):
    g = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY)
    # Equaliza o contraste local: uma foto um pouco mais escura acha os
    # mesmos pontos.
    return cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8)).apply(g)


def align(base_rgb, src_rgb, exclude=None) -> dict:
    """Transformacao que leva ``src`` sobre ``base`` (pixels de cada imagem).

    ``exclude`` (uint8 do tamanho de ``src_rgb``, >0 = fora) tira da analise a
    area que vai ser extraida — o alinhamento usa so o que esta EM VOLTA dela.

    Devolve ``{'matrix': 2x3, 'inliers', 'error', 'ok', 'gain': [r, g, b]}``;
    sem alinhamento confiavel, a matriz e a identidade (na escala) e ``ok``
    e falso."""
    sift = cv2.SIFT_create(nfeatures=FEATURES)
    kb, db = sift.detectAndCompute(_gray(base_rgb), None)
    keep = None
    if exclude is not None:
        # Com folga: a borda do objeto tambem se mexeu.
        grow = max(src_rgb.shape[:2]) // 80 * 2 + 1
        keep = np.where(cv2.dilate((exclude > 0).astype(np.uint8), np.ones((grow, grow), np.uint8)) > 0,
                        0, 255).astype(np.uint8)
    ks, ds = sift.detectAndCompute(_gray(src_rgb), keep)
    bh, bw = base_rgb.shape[:2]
    sh, sw = src_rgb.shape[:2]
    identity = np.array([[bw / sw, 0, 0], [0, bh / sh, 0]], np.float64)
    fail = {'matrix': identity.tolist(), 'inliers': 0, 'error': None,
            'ok': False, 'gain': [1.0, 1.0, 1.0]}
    if db is None or ds is None or len(kb) < MIN_INLIERS or len(ks) < MIN_INLIERS:
        return fail

    pairs = cv2.BFMatcher(cv2.NORM_L2).knnMatch(ds, db, k=2)
    good = [p[0] for p in pairs if len(p) == 2 and p[0].distance < LOWE_RATIO * p[1].distance]
    if len(good) < MIN_INLIERS:
        return fail
    src = np.float32([ks[m.queryIdx].pt for m in good])
    dst = np.float32([kb[m.trainIdx].pt for m in good])
    matrix, mask = cv2.estimateAffinePartial2D(
        src, dst, method=cv2.RANSAC, ransacReprojThreshold=RANSAC_PX,
        maxIters=5000, confidence=0.999)
    if matrix is None:
        return fail
    inl = mask.ravel().astype(bool)
    if inl.sum() < MIN_INLIERS:
        return fail
    # Refina so com o fundo: o RANSAC escolhe, o minimo quadrado ajusta.
    refined, _ = cv2.estimateAffinePartial2D(src[inl], dst[inl], method=cv2.LMEDS)
    if refined is not None:
        matrix = refined
    err = np.linalg.norm(src[inl] @ matrix[:, :2].T + matrix[:, 2] - dst[inl], axis=1)
    return {'matrix': matrix.tolist(), 'inliers': int(inl.sum()),
            'error': round(float(np.median(err)), 3), 'ok': True,
            'gain': gain(base_rgb, src_rgb, matrix)}


def gain(base_rgb, src_rgb, matrix) -> list:
    """Ganho por canal que iguala a luz de ``src`` (alinhada) a da base,
    medido so no fundo — onde as duas fotos ja coincidem."""
    h, w = base_rgb.shape[:2]
    warped = cv2.warpAffine(src_rgb, np.asarray(matrix, np.float64), (w, h),
                            flags=cv2.INTER_LINEAR, borderValue=0)
    valid = cv2.warpAffine(np.full(src_rgb.shape[:2], 255, np.uint8),
                           np.asarray(matrix, np.float64), (w, h), flags=cv2.INTER_NEAREST) > 0
    b = base_rgb.astype(np.float32)
    s = warped.astype(np.float32)
    static = valid & (np.abs(b - s).max(axis=2) < STATIC_DIFF)
    # Nem estourado nem preto: la o sensor nao mede a luz.
    lum = b.mean(axis=2)
    static &= (lum > 10) & (lum < 245)
    if static.sum() < 1000:
        return [1.0, 1.0, 1.0]
    out = []
    for c in range(3):
        ratio = float(b[..., c][static].mean()) / max(float(s[..., c][static].mean()), 1.0)
        out.append(round(min(max(ratio, GAIN_RANGE[0]), GAIN_RANGE[1]), 4))
    return out


def describe(matrix) -> dict:
    """Escala, giro (graus) e deslocamento de uma similaridade 2x3."""
    m = np.asarray(matrix, np.float64)
    return {'scale': round(math.hypot(m[0, 0], m[1, 0]), 5),
            'angle': round(math.degrees(math.atan2(m[1, 0], m[0, 0])), 4),
            'dx': round(float(m[0, 2]), 2), 'dy': round(float(m[1, 2]), 2)}


def scaled(matrix, src_from, src_to, dst_from, dst_to) -> np.ndarray:
    """A mesma transformacao em outra resolucao: ``matrix`` leva pixels de
    ``src_from`` (w, h) a ``dst_from``; a devolvida, de ``src_to`` a
    ``dst_to``."""
    m = np.vstack([np.asarray(matrix, np.float64), [0, 0, 1]])
    s_in = np.diag([src_from[0] / src_to[0], src_from[1] / src_to[1], 1.0])
    s_out = np.diag([dst_to[0] / dst_from[0], dst_to[1] / dst_from[1], 1.0])
    return (s_out @ m @ s_in)[:2]


def cutout(src_rgb, alpha, matrix, gain_rgb, out_size, offset=(0, 0)):
    """Area da foto levada sobre a base: ``(rgb, alpha, (x0, y0))`` so da
    caixa que ela ocupa na base, ou ``None`` se ficou vazia/fora do quadro.

    ``alpha`` (float em [0, 1], do tamanho de ``src_rgb``) e a mascara da area;
    ``matrix`` leva a foto a base de ``out_size`` (w, h). ``src_rgb`` pode ser
    um recorte da foto que comeca em ``offset``. So a caixa e transformada: na
    resolucao cheia, girar a foto inteira por uma bola custaria 70 MB e um
    segundo por camada."""
    ys, xs = np.nonzero(alpha > 1e-3)
    if not len(xs):
        return None
    w, h = out_size
    m = np.asarray(matrix, np.float64).copy()
    # Coordenadas do recorte -> da foto inteira.
    m[:, 2] += m[:, :2] @ np.asarray(offset, np.float64)
    corners = np.array([[xs.min(), ys.min()], [xs.max() + 1, ys.min()],
                        [xs.min(), ys.max() + 1], [xs.max() + 1, ys.max() + 1]], np.float64)
    mapped = corners @ m[:, :2].T + m[:, 2]
    x0 = max(int(math.floor(mapped[:, 0].min())) - 2, 0)
    y0 = max(int(math.floor(mapped[:, 1].min())) - 2, 0)
    x1 = min(int(math.ceil(mapped[:, 0].max())) + 2, w)
    y1 = min(int(math.ceil(mapped[:, 1].max())) + 2, h)
    if x1 <= x0 or y1 <= y0:
        return None
    # Desloca a origem para o canto da caixa.
    local = m.copy()
    local[:, 2] -= (x0, y0)
    size = (x1 - x0, y1 - y0)
    rgb = cv2.warpAffine(src_rgb, local, size, flags=cv2.INTER_LINEAR,
                         borderMode=cv2.BORDER_REPLICATE)
    a = cv2.warpAffine(alpha.astype(np.float32), local, size, flags=cv2.INTER_LINEAR,
                       borderValue=0)
    rgb = np.clip(rgb.astype(np.float32) * np.asarray(gain_rgb, np.float32), 0, 255)
    return rgb, np.clip(a, 0.0, 1.0), (x0, y0)


def backdrop(src_rgb, alpha, origin, matrix, gain_rgb):
    """O que a foto de origem mostra ATRAS de uma area da base: ``alpha``
    (float, a mascara da area ja recortada na caixa que comeca em ``origin``
    da base) e a regiao da origem, alinhada, que cai nessa caixa.

    E o fundo limpo para esmaecer um objeto da propria base (a bola da
    primeira foto): numa foto seguinte o objeto ja saiu dali. Devolve uma
    peca no formato de ``cutout``, para o ``paste``."""
    h, w = alpha.shape
    m = np.asarray(matrix, np.float64).copy()
    m[:, 2] -= origin
    rgb = cv2.warpAffine(src_rgb, m, (w, h), flags=cv2.INTER_LINEAR,
                         borderMode=cv2.BORDER_REPLICATE)
    rgb = np.clip(rgb.astype(np.float32) * np.asarray(gain_rgb, np.float32), 0, 255)
    return rgb, alpha, tuple(origin)


def paste(canvas, piece, opacity):
    """Compoe ``piece`` (saida de ``cutout``) sobre ``canvas`` (RGB uint8,
    alterado no lugar) com a transparencia da camada. So a caixa da area passa
    por float: na resolucao cheia, a foto inteira em float seriam 290 MB."""
    rgb, a, (x0, y0) = piece
    h, w = a.shape
    a = (a * float(opacity))[..., None]
    region = canvas[y0:y0 + h, x0:x0 + w]
    mixed = region.astype(np.float32) * (1.0 - a) + rgb * a
    region[...] = np.clip(mixed + 0.5, 0, 255).astype(np.uint8)

"""Selecao por pincel: traco sobre a foto -> mascara do objeto pintado.

Quem acha o objeto e o SAM 2.1 tiny (Segment Anything 2, Meta, Apache-2.0)
em ONNX, rodando em CPU pelo onnxruntime. O modelo tem duas metades:

* o **encoder** le a foto inteira e devolve os embeddings (~1,3 s numa CPU
  comum). Roda UMA vez por foto: o resultado fica num cache em memoria;
* o **decoder** recebe pontos/caixa e devolve 3 mascaras candidatas em ~25 ms.

Por que nao GrabCut nem Ollama (medido em fotos de ginasio, 2026-09-24): o
GrabCut do OpenCV so separa cor, e vaza para o fundo quando o fundo tem
textura (janelas, arquibancada); os modelos de visao do Ollama respondem com
texto e, no maximo, caixas — nenhum devolve mascara por pixel.

O traco da pessoa COBRE o objeto (como no Lightroom). O SAM, com um ponto,
tende a devolver uma PARTE (o rosto, o braco); por isso cada traco vira
varios prompts (caixa, pontos no miolo, os dois) e, das mascaras candidatas,
vence a que melhor coincide (IoU) com a area pintada. Custa ~12 decodes por
traco, ~0,3 s, e acerta a pessoa inteira.

Memoria: o encoder chega a ~1,2 GB de pico (sem o arena do onnxruntime, que
seguraria esse pico para sempre). Um lock serializa as execucoes: duas fotos
ao mesmo tempo dobrariam o pico.
"""
from __future__ import annotations

import logging
import threading
from collections import OrderedDict

import cv2
import numpy as np
from django.conf import settings

from photoeditor.imaging.develop import _guided, luma

log = logging.getLogger(__name__)

INPUT_SIDE = 1024
MEAN = np.array([0.485, 0.456, 0.406], np.float32)
STD = np.array([0.229, 0.224, 0.225], np.float32)
ENCODER = 'vision_encoder.onnx'
DECODER = 'prompt_encoder_mask_decoder.onnx'
# ~16 MB por foto: cobre ir e voltar entre as vizinhas sem reprocessar.
EMBED_CACHE = 4
THREADS = 4

_lock = threading.Lock()
_sessions = None
_embeddings: OrderedDict = OrderedDict()


def available() -> bool:
    """Modelo presente e onnxruntime instalado. Sem ele, o pincel vale como
    esta (a area pintada vira a mascara), sem deteccao de objeto."""
    model = settings.SEGMENT_MODEL_DIR
    if not ((model / ENCODER).is_file() and (model / DECODER).is_file()):
        return False
    try:
        import onnxruntime  # noqa: F401
    except ImportError:
        return False
    return True


def _load():
    """Sessoes do encoder e do decoder, criadas na primeira selecao."""
    global _sessions
    if _sessions is None:
        import onnxruntime as ort
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = THREADS
        opts.enable_cpu_mem_arena = False
        model = settings.SEGMENT_MODEL_DIR
        _sessions = (ort.InferenceSession(str(model / ENCODER), opts),
                     ort.InferenceSession(str(model / DECODER), opts))
        log.info("Segmentation model loaded from %s", model)
    return _sessions


def _embed(key, rgb):
    """Embeddings da foto (chave = foto + versao do arquivo), com cache."""
    if key in _embeddings:
        _embeddings.move_to_end(key)
        return _embeddings[key]
    encoder, _ = _load()
    x = cv2.resize(rgb, (INPUT_SIDE, INPUT_SIDE), interpolation=cv2.INTER_AREA)
    x = ((x.astype(np.float32) / 255.0 - MEAN) / STD).transpose(2, 0, 1)[None]
    names = [o.name for o in encoder.get_outputs()]
    emb = dict(zip(names, encoder.run(None, {'pixel_values': x})))
    _embeddings[key] = emb
    while len(_embeddings) > EMBED_CACHE:
        _embeddings.popitem(last=False)
    return emb


def warm(key, rgb):
    """Calcula os embeddings antes do primeiro traco (ao entrar no modo)."""
    with _lock:
        _embed(key, rgb)


# --------------------------------------------------------------------------- #
# Traco -> area pintada
# --------------------------------------------------------------------------- #

def paint(shape, points, radius) -> np.ndarray:
    """Area coberta por um traco: ``points`` em fracao da largura/altura e
    ``radius`` em fracao do lado maior (o tamanho do pincel na tela)."""
    h, w = shape[:2]
    r = max(int(round(radius * max(h, w))), 1)
    pts = [(int(round(x * w)), int(round(y * h))) for x, y in points]
    out = np.zeros((h, w), np.uint8)
    for a, b in zip(pts, pts[1:]):
        cv2.line(out, a, b, 255, 2 * r, cv2.LINE_AA)
    for p in pts:
        cv2.circle(out, p, r, 255, -1, cv2.LINE_AA)
    return out > 127


# --------------------------------------------------------------------------- #
# Area pintada -> objeto
# --------------------------------------------------------------------------- #

def _inner_points(region, n):
    """``n`` pontos espalhados no MIOLO da area (longe da borda do traco,
    onde a pincelada quase certamente esta sobre o objeto)."""
    dist = cv2.distanceTransform(region.astype(np.uint8), cv2.DIST_L2, 5)
    ys, xs = np.nonzero(dist >= 0.5 * dist.max())
    if not len(xs):
        ys, xs = np.nonzero(region)
    order = np.lexsort((xs, ys))
    pick = order[np.linspace(0, len(order) - 1, n).astype(int)]
    return np.stack([xs[pick], ys[pick]], 1).astype(np.float32)


def _iou(a, b) -> float:
    return float((a & b).sum()) / max(float((a | b).sum()), 1.0)


def _object(emb, shape, region) -> np.ndarray:
    """Mascara do objeto sob uma area pintada conexa."""
    _, decoder = _load()
    h, w = shape[:2]
    scale = np.array([INPUT_SIDE / w, INPUT_SIDE / h], np.float32)
    ys, xs = np.nonzero(region)
    box = np.array([xs.min(), ys.min(), xs.max(), ys.max()], np.float32)
    no_points = np.zeros((0, 2), np.float32)
    prompts = [(None, box), (_inner_points(region, 1), None),
               (_inner_points(region, 3), None), (_inner_points(region, 3), box),
               (_inner_points(region, 5), box)]
    best, best_score = region, -1.0
    for points, bx in prompts:
        pts = no_points if points is None else points
        feed = dict(emb)
        feed['input_points'] = (pts * scale)[None, None]
        feed['input_labels'] = np.ones((1, 1, len(pts)), np.int64)
        feed['input_boxes'] = (np.zeros((1, 0, 4), np.float32) if bx is None
                               else (bx.reshape(2, 2) * scale).reshape(1, 1, 4))
        _, masks, _ = decoder.run(None, feed)
        for logits in masks[0, 0]:
            mask = cv2.resize(logits, (w, h), interpolation=cv2.INTER_LINEAR) > 0
            score = _iou(mask, region)
            if score > best_score:
                best, best_score = mask, score
    return _clean(best)


def _clean(mask, min_frac=0.02):
    """Tira ilhas soltas (menores que 2 % da maior parte) e tapa furos
    pequenos: o upscale das mascaras de 256 px deixa pontilhado na borda."""
    n, lab, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), connectivity=8)
    if n > 1:
        areas = stats[1:, cv2.CC_STAT_AREA]
        keep = np.concatenate([[False], areas >= areas.max() * min_frac])
        mask = keep[lab]
    holes = (~mask).astype(np.uint8)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(holes, connectivity=4)
    h, w = mask.shape
    limit = max(mask.sum() * 0.01, 16)
    for i in range(1, n):
        x, y, bw, bh, area = stats[i]
        if x > 0 and y > 0 and x + bw < w and y + bh < h and area < limit:
            mask[lab == i] = True
    return mask


def refine(rgb, mask, smart=True) -> np.ndarray:
    """Borda macia (float32 em [0, 1]). Com o objeto detectado, o guided
    filter encosta a borda no contorno real da foto; o pincel puro so e
    suavizado, como um pincel de borda macia."""
    m = mask.astype(np.float32)
    side = max(mask.shape)
    if smart:
        guide = luma(rgb.astype(np.float32) / 255.0).astype(np.float32)
        # Janela ~ o erro do upscale da mascara (256 px -> lado da foto).
        m = _guided(m, guide, r=max(side // 160, 3), eps=1e-3)
    else:
        k = max(side // 300, 1) * 2 + 1
        m = cv2.GaussianBlur(m, (k, k), 0)
    return np.clip(m, 0.0, 1.0)


def select(key, rgb, points, radius, smart=True) -> np.ndarray:
    """Um traco -> mascara macia (float32 em [0, 1], do tamanho de ``rgb``).

    Com ``smart``, cada parte conexa do traco vira o objeto que ela cobre;
    sem, a propria area pintada."""
    region = paint(rgb.shape, points, radius)
    if not region.any():
        return np.zeros(rgb.shape[:2], np.float32)
    if not smart:
        return refine(rgb, region, smart=False)
    with _lock:
        emb = _embed(key, rgb)
        n, lab = cv2.connectedComponents(region.astype(np.uint8), connectivity=8)
        found = np.zeros(region.shape, bool)
        for i in range(1, n):
            found |= _object(emb, rgb.shape, lab == i)
    return refine(rgb, found)

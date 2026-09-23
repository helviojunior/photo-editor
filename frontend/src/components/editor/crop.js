/**
 * Crop na proporção da foto — espelho de `imaging/develop.py:normalize_crop`.
 *
 * O quadro é a própria foto reduzida por `scale`, centrada em (cx, cy) —
 * frações da largura e da altura — e girada `angle` graus (horário, como o
 * `rotate()` do CSS). O backend normaliza de novo ao gravar; aqui a regra
 * existe para o quadro não sair da foto enquanto é arrastado.
 */
export const CROP_IDENTITY = { scale: 1, cx: 0.5, cy: 0.5, angle: 0 };
export const CROP_MIN_SCALE = 0.1;
export const CROP_MAX_ANGLE = 45;

const rad = (deg) => (deg * Math.PI) / 180;
const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);
const round = (v, n) => Math.round(v * 10 ** n) / 10 ** n;

// Maior escala em que o quadro girado ainda cabe (aspect = altura/largura).
export function cropMaxScale(angle, aspect) {
  const c = Math.abs(Math.cos(rad(angle)));
  const s = Math.abs(Math.sin(rad(angle)));
  return Math.min(1, 1 / (c + aspect * s), 1 / (s / aspect + c));
}

// Meia caixa envolvente do quadro, em fração da largura e da altura.
export function cropExtents(crop, aspect) {
  const c = Math.abs(Math.cos(rad(crop.angle)));
  const s = Math.abs(Math.sin(rad(crop.angle)));
  return [(crop.scale / 2) * (c + aspect * s), (crop.scale / 2) * (s / aspect + c)];
}

export function isCropIdentity(crop) {
  if (!crop) return true;
  return crop.angle === 0 && crop.scale >= 0.9999
    && Math.abs(crop.cx - 0.5) < 1e-4 && Math.abs(crop.cy - 0.5) < 1e-4;
}

export function normalizeCrop(crop, aspect) {
  const c = { ...CROP_IDENTITY, ...(crop || {}) };
  const a = aspect > 0 ? aspect : 1;
  const angle = round(clamp(c.angle, -CROP_MAX_ANGLE, CROP_MAX_ANGLE), 1);
  const scale = clamp(c.scale, CROP_MIN_SCALE, cropMaxScale(angle, a));
  const [ex, ey] = cropExtents({ scale, angle }, a);
  const out = {
    scale: round(scale, 4),
    cx: round(clamp(c.cx, ex, 1 - ex), 4),
    cy: round(clamp(c.cy, ey, 1 - ey), 4),
    angle,
  };
  return isCropIdentity(out) ? { ...CROP_IDENTITY } : out;
}

export function sameCrop(a, b) {
  const x = a || CROP_IDENTITY;
  const y = b || CROP_IDENTITY;
  return x.scale === y.scale && x.cx === y.cx && x.cy === y.cy && x.angle === y.angle;
}

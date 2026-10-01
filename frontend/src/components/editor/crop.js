/**
 * Crop — espelho de `imaging/develop.py:normalize_crop`.
 *
 * O quadro tem uma proporção (`ratio`, altura/largura): 0 = a da foto (o crop
 * normal); a versão Instagram usa uma das proporções do feed. Com `scale` 1 e
 * sem giro, ele é o maior dessa proporção que cabe na foto (`cropFrame`);
 * `scale` o reduz, o centro fica em (cx, cy) — frações da largura e da altura
 * — e `angle` o gira (horário, como o `rotate()` do CSS). O backend
 * normaliza de novo ao gravar; aqui a regra existe para o quadro não sair da
 * foto enquanto é arrastado.
 */
export const CROP_IDENTITY = { scale: 1, cx: 0.5, cy: 0.5, angle: 0, ratio: 0 };
export const CROP_MIN_SCALE = 0.1;
export const CROP_MAX_ANGLE = 90;
// Versão Instagram: em 4:5 e 1:1 o giro só endireita (±45°); o 1,91:1 vem
// do backend com ±90°, a regra do crop normal (passando de 45° ele deita e o
// recorte sai vertical, 1:1,91 — fora do feed).
export const INSTAGRAM_MAX_ANGLE = 45;

const rad = (deg) => (deg * Math.PI) / 180;
const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);
const round = (v, n) => Math.round(v * 10 ** n) / 10 ** n;

// Quadro com scale 1, em fração da largura e da altura da foto.
export function cropFrame(ratio, aspect) {
  if (!ratio || ratio <= 0) return [1, 1];
  return [Math.min(1, aspect / ratio), Math.min(1, ratio / aspect)];
}

// Maior escala em que o quadro girado ainda cabe (aspect = altura/largura).
export function cropMaxScale(angle, aspect, ratio = 0) {
  const c = Math.abs(Math.cos(rad(angle)));
  const s = Math.abs(Math.sin(rad(angle)));
  const [bw, bh] = cropFrame(ratio, aspect);
  return Math.min(1, 1 / (bw * c + bh * aspect * s), 1 / ((bw * s) / aspect + bh * c));
}

// Meia caixa envolvente do quadro, em fração da largura e da altura.
export function cropExtents(crop, aspect) {
  const c = Math.abs(Math.cos(rad(crop.angle)));
  const s = Math.abs(Math.sin(rad(crop.angle)));
  const [bw, bh] = cropFrame(crop.ratio, aspect);
  return [(crop.scale / 2) * (bw * c + bh * aspect * s), (crop.scale / 2) * ((bw * s) / aspect + bh * c)];
}

// A proporção da lista mais perto de `target`, em escala log.
export function closestRatio(target, ratios) {
  const t = target > 0 ? target : 1;
  return ratios.reduce((best, r) => (
    Math.abs(Math.log(r / t)) < Math.abs(Math.log(best / t)) ? r : best), ratios[0]);
}

export function isCropIdentity(crop) {
  if (!crop) return true;
  return crop.angle === 0 && crop.scale >= 0.9999 && !crop.ratio
    && Math.abs(crop.cx - 0.5) < 1e-4 && Math.abs(crop.cy - 0.5) < 1e-4;
}

/**
 * Sem `ratios`, o quadro tem a proporção da foto. Com elas (a versão
 * Instagram: `[{ratio, max_angle}]`, de /api/develop/), `ratio` é sempre uma
 * delas e o giro fica no limite dela.
 */
export function normalizeCrop(crop, aspect, ratios = null) {
  const c = { ...CROP_IDENTITY, ...(crop || {}) };
  const a = aspect > 0 ? aspect : 1;
  const values = ratios?.length ? ratios.map((r) => r.ratio) : null;
  const ratio = values ? round(closestRatio(c.ratio || a, values), 4) : 0;
  const maxAngle = values
    ? ratios.find((r) => r.ratio === ratio)?.max_angle ?? INSTAGRAM_MAX_ANGLE
    : CROP_MAX_ANGLE;
  const angle = round(clamp(c.angle, -maxAngle, maxAngle), 1);
  const scale = clamp(c.scale, CROP_MIN_SCALE, cropMaxScale(angle, a, ratio));
  const [ex, ey] = cropExtents({ scale, angle, ratio }, a);
  const out = {
    scale: round(scale, 4),
    cx: round(clamp(c.cx, ex, 1 - ex), 4),
    cy: round(clamp(c.cy, ey, 1 - ey), 4),
    angle,
    ratio,
  };
  return isCropIdentity(out) ? { ...CROP_IDENTITY } : out;
}

// Quartos de volta e resto (em [-45, 45)) de um ângulo do quadro.
function splitAngle(angle) {
  const quarters = Math.floor((angle + 45) / 90);
  return [quarters, angle - 90 * quarters];
}

// Altura/largura do recorte que sai (a 90° o quadro deita e a troca).
export function cropOutputAspect(crop, aspect) {
  const [bw, bh] = cropFrame(crop?.ratio, aspect);
  const out = (bh * aspect) / bw;
  return splitAngle(crop?.angle || 0)[0] % 2 ? 1 / out : out;
}

// O recorte cabe no que o feed aceita? (`feedAspect` = [min, max] do backend)
export function feedOk(crop, aspect, feedAspect) {
  if (!feedAspect) return true;
  const out = cropOutputAspect(crop, aspect);
  return out >= feedAspect[0] && out <= feedAspect[1];
}

export function sameCrop(a, b) {
  const x = a || CROP_IDENTITY;
  const y = b || CROP_IDENTITY;
  return x.scale === y.scale && x.cx === y.cx && x.cy === y.cy && x.angle === y.angle
    && (x.ratio || 0) === (y.ratio || 0);
}

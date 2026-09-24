import { isCropIdentity, sameCrop } from "./crop";

/**
 * URL do preview editado para um rascunho de ajustes — a mesma que o backend
 * monta em `photo_json` (views/photos.py:render_url). Versão do arquivo (`v`)
 * e do motor (`e`) vêm da `edited_url` que o backend mandou.
 */
export default function renderUrl(photo, draft) {
  if (!photo || !draft) return null;
  const base = new URL(photo.edited_url, window.location.origin);
  const params = new URLSearchParams();
  params.set("v", base.searchParams.get("v") || "");
  params.set("e", base.searchParams.get("e") || "");
  Object.entries(draft.values).forEach(([k, v]) => { if (v) params.set(k, String(v)); });
  if (draft.preset) params.set("preset", draft.preset);
  if (draft.crop && !isCropIdentity(draft.crop)) {
    ["scale", "cx", "cy", "angle"].forEach((k) => params.set(`crop_${k}`, String(draft.crop[k])));
  }
  if (draft.layers?.length) params.set("layers", layersParam(draft.layers));
  return `${base.pathname}?${params.toString()}`;
}

// Formato curto do backend (views/photos.py:layers_param), chaves em ordem.
function layersParam(layers) {
  return JSON.stringify(layers.map((l) => {
    const v = {};
    Object.keys(l.values).sort().forEach((k) => { if (l.values[k]) v[k] = l.values[k]; });
    return { m: l.mask, p: l.preset || "", v };
  }));
}

function sameValues(a, b) {
  const keys = new Set([...Object.keys(a || {}), ...Object.keys(b || {})]);
  return [...keys].every((k) => ((a || {})[k] || 0) === ((b || {})[k] || 0));
}

function sameLayers(a, b) {
  const x = a || [];
  const y = b || [];
  return x.length === y.length && x.every((l, i) => l.id === y[i].id && l.mask === y[i].mask
    && (l.preset || "") === (y[i].preset || "") && sameValues(l.values, y[i].values));
}

export function sameState(a, b) {
  if (!a || !b || a.preset !== b.preset || !sameCrop(a.crop, b.crop)) return false;
  return sameValues(a.values, b.values) && sameLayers(a.layers, b.layers);
}

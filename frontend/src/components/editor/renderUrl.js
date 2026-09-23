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
  return `${base.pathname}?${params.toString()}`;
}

export function sameState(a, b) {
  if (!a || !b || a.preset !== b.preset || !sameCrop(a.crop, b.crop)) return false;
  const keys = new Set([...Object.keys(a.values), ...Object.keys(b.values)]);
  return [...keys].every((k) => (a.values[k] || 0) === (b.values[k] || 0));
}

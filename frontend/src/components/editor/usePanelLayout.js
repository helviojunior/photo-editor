import { useCallback, useEffect, useState } from "react";

/**
 * Tamanhos dos painéis do editor, arrastáveis e lembrados entre sessões.
 *
 *   top    fração da altura para a área de cima (fotos + painel); o resto é
 *          da filmstrip. O padrão, 70%, é o layout original (TODO 3.1).
 *   split  fração da largura das fotos que fica com a Original.
 *   panel  largura do painel de edição, em px.
 *
 * Guardado no localStorage: é preferência de quem usa ESTA máquina, e perder
 * (navegação privada, storage limpo) só volta ao padrão.
 */
const STORAGE_KEY = "photoeditor.editor.layout";

export const LAYOUT_DEFAULTS = { top: 0.7, split: 0.5, panel: 288 };
export const LAYOUT_LIMITS = {
  top: [0.35, 0.85],
  split: [0.2, 0.8],
  panel: [240, 560],
};
// Passo das setas do teclado sobre a divisória.
export const LAYOUT_STEPS = { top: 0.02, split: 0.02, panel: 16 };

const clamp = (key, value) => {
  const [min, max] = LAYOUT_LIMITS[key];
  return Math.min(max, Math.max(min, value));
};

function read() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) || "{}");
    return Object.fromEntries(Object.entries(LAYOUT_DEFAULTS).map(([k, v]) => [
      k, typeof saved[k] === "number" && Number.isFinite(saved[k]) ? clamp(k, saved[k]) : v,
    ]));
  } catch {
    return { ...LAYOUT_DEFAULTS };
  }
}

export default function usePanelLayout() {
  const [layout, setLayout] = useState(read);

  useEffect(() => {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(layout)); } catch { /* sem storage */ }
  }, [layout]);

  const set = useCallback((key, value) => {
    setLayout((l) => ({ ...l, [key]: clamp(key, value) }));
  }, []);
  const step = useCallback((key, direction) => {
    setLayout((l) => ({ ...l, [key]: clamp(key, l[key] + direction * LAYOUT_STEPS[key]) }));
  }, []);
  const reset = useCallback((key) => {
    setLayout((l) => ({ ...l, [key]: LAYOUT_DEFAULTS[key] }));
  }, []);

  return { layout, set, step, reset };
}

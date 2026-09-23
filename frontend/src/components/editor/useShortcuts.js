import { useEffect, useRef } from "react";

// Foco num campo (texto, select, slider): as teclas sao do campo, nao do
// editor — a seta move o slider, o Backspace apaga o texto (TODO 5.6).
function isFieldTarget(el) {
  if (!el) return false;
  const tag = el.tagName;
  return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || el.isContentEditable;
}

/**
 * Atalhos do editor: → proxima, ← anterior, DEL excluir, CTRL/CMD+Z desfazer,
 * C entra/sai do modo crop (no modo crop, quem decide o que as setas fazem e
 * o Editor: elas giram o quadro).
 *
 * O DEL aceita tambem o Backspace: no teclado do Mac a tecla "delete" manda
 * Backspace, e o Delete de verdade so existe com fn.
 */
export default function useShortcuts({ onNext, onPrev, onDelete, onUndo, onCrop }) {
  // Os handlers mudam a cada render; o listener fica um so.
  const handlers = useRef({});
  handlers.current = { onNext, onPrev, onDelete, onUndo, onCrop };

  useEffect(() => {
    const onKeyDown = (e) => {
      if (e.defaultPrevented || isFieldTarget(e.target)) return;
      // Com um modal aberto, o teclado e dele.
      if (document.querySelector('[role="dialog"]')) return;

      const h = handlers.current;
      const mod = e.ctrlKey || e.metaKey;
      let handler = null;
      if (mod && !e.shiftKey && !e.altKey && e.key.toLowerCase() === "z") handler = h.onUndo;
      else if (mod || e.altKey) return;
      else if (e.key === "ArrowRight") handler = h.onNext;
      else if (e.key === "ArrowLeft") handler = h.onPrev;
      else if (e.key === "Delete" || e.key === "Backspace") handler = h.onDelete;
      else if (e.key.toLowerCase() === "c" && !e.shiftKey) handler = h.onCrop;

      if (!handler) return;
      e.preventDefault();
      if (!e.repeat || e.key.startsWith("Arrow")) handler();
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);
}

import { useEffect, useState } from "react";

/**
 * Maior retângulo na proporção da foto (`aspect` = altura/largura) que cabe no
 * painel, com 8px de respiro em cada lado. É a caixa exata da imagem na tela:
 * o que é desenhado por cima (quadro do crop, máscara da seleção) usa frações
 * dela e cai no mesmo pixel da foto.
 */
export default function useFitSize(paneRef, aspect) {
  const [size, setSize] = useState({ w: 0, h: 0 });

  useEffect(() => {
    const pane = paneRef.current;
    if (!pane) return undefined;
    const fit = () => {
      const pw = pane.clientWidth - 16;
      const ph = pane.clientHeight - 16;
      const w = Math.max(Math.min(pw, ph / aspect), 0);
      setSize({ w, h: w * aspect });
    };
    fit();
    const ro = new ResizeObserver(fit);
    ro.observe(pane);
    return () => ro.disconnect();
  }, [paneRef, aspect]);

  return size;
}

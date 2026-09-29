import React, { useRef } from "react";
import { cn } from "lib/utils";

/**
 * Divisória arrastável entre dois painéis.
 *
 * `orientation="vertical"` separa colunas (arrasta na horizontal);
 * `"horizontal"` separa linhas (arrasta na vertical). Quem sabe converter a
 * posição do ponteiro em tamanho é o dono do layout: `onDrag` recebe o
 * evento do ponteiro (`clientX`/`clientY`) a cada movimento.
 *
 * Teclado (é um `role="separator"` focável): setas chamam `onStep(-1|+1)`,
 * Home/End e duplo clique chamam `onReset`.
 *
 * O alvo do mouse é mais largo que a linha visível (6px contra 1px): acertar
 * uma linha de 1px com o ponteiro é frustrante.
 */
export function Splitter({
  orientation = "vertical",
  label,
  value,
  onDrag,
  onStep,
  onReset,
  className,
}) {
  const dragging = useRef(false);
  const vertical = orientation === "vertical";

  const onPointerDown = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    dragging.current = true;
    e.currentTarget.setPointerCapture(e.pointerId);
    // Durante o arraste: nada de selecionar texto nem trocar o cursor ao
    // passar por cima de outro elemento.
    document.body.style.userSelect = "none";
    document.body.style.cursor = vertical ? "col-resize" : "row-resize";
  };

  const stop = (e) => {
    if (!dragging.current) return;
    dragging.current = false;
    e.currentTarget.releasePointerCapture?.(e.pointerId);
    document.body.style.userSelect = "";
    document.body.style.cursor = "";
  };

  const onKeyDown = (e) => {
    const back = vertical ? "ArrowLeft" : "ArrowUp";
    const forward = vertical ? "ArrowRight" : "ArrowDown";
    if (e.key === back || e.key === forward) {
      e.preventDefault();
      // Sem propagar: as setas também trocam de foto no editor.
      e.stopPropagation();
      onStep?.(e.key === forward ? 1 : -1);
    } else if (e.key === "Home" || e.key === "End") {
      e.preventDefault();
      e.stopPropagation();
      onReset?.();
    }
  };

  return (
    <div
      role="separator"
      aria-orientation={vertical ? "vertical" : "horizontal"}
      aria-label={label}
      aria-valuenow={value != null ? Math.round(value) : undefined}
      aria-valuemin={0}
      aria-valuemax={100}
      title={label}
      tabIndex={0}
      onPointerDown={onPointerDown}
      onPointerMove={(e) => dragging.current && onDrag?.(e)}
      onPointerUp={stop}
      onPointerCancel={stop}
      onDoubleClick={() => onReset?.()}
      onKeyDown={onKeyDown}
      className={cn(
        "group relative z-20 flex shrink-0 touch-none items-center justify-center",
        "bg-border focus-visible:outline-none",
        vertical ? "w-px cursor-col-resize" : "h-px cursor-row-resize",
        className
      )}
    >
      {/* Área de acerto maior que a linha, centrada nela. */}
      <span
        aria-hidden="true"
        className={cn(
          "absolute transition-colors group-hover:bg-brand-400/60 group-focus-visible:bg-brand-400",
          vertical ? "inset-y-0 -left-[3px] w-[7px]" : "inset-x-0 -top-[3px] h-[7px]"
        )}
      />
    </div>
  );
}

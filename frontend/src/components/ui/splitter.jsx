import React, { useRef, useState } from "react";
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
 * Visual: uma faixa de 5px NO TOTAL (as duas linhas paralelas de 1px + 3px
 * entre elas), com a "pegada" no meio — três bolinhas SÓLIDAS de 3px, na
 * direção da divisória, preenchidas na mesma cor das linhas da barra.
 * Ao passar o mouse, arrastar ou focar pelo teclado a faixa INTEIRA fica num
 * vermelho suavizado (acento a 60% sobre o fundo escuro), com as bolinhas
 * num branco suave para continuarem visíveis. A faixa inteira é
 * o alvo do ponteiro; linhas e pegada ganham a cor de acento ao passar o
 * mouse, arrastar ou focar pelo teclado.
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
  // Estado (e nao so a ref) para a faixa ficar acesa enquanto arrasta, mesmo
  // com o ponteiro fora dela.
  const [active, setActive] = useState(false);
  const vertical = orientation === "vertical";

  const onPointerDown = (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    dragging.current = true;
    setActive(true);
    e.currentTarget.setPointerCapture(e.pointerId);
    // Durante o arraste: nada de selecionar texto nem trocar o cursor ao
    // passar por cima de outro elemento.
    document.body.style.userSelect = "none";
    document.body.style.cursor = vertical ? "col-resize" : "row-resize";
  };

  const stop = (e) => {
    if (!dragging.current) return;
    dragging.current = false;
    setActive(false);
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
      data-active={active || undefined}
      className={cn(
        "group relative z-20 flex shrink-0 touch-none select-none items-center justify-center",
        "bg-background text-border transition-colors focus-visible:outline-none",
        "hover:border-brand-400/60 hover:bg-brand-400/60 hover:text-white/70",
        "focus-visible:border-brand-400/60 focus-visible:bg-brand-400/60 focus-visible:text-white/70",
        "data-[active]:border-brand-400/60 data-[active]:bg-brand-400/60 data-[active]:text-white/70",
        vertical
          ? "w-[5px] cursor-col-resize flex-col border-x border-border"
          : "h-[5px] cursor-row-resize flex-row border-y border-border",
        className
      )}
    >
      {/* A pegada: três círculos na direção da divisória. */}
      <span aria-hidden="true" className={cn("flex gap-[2px]", vertical ? "flex-col" : "flex-row")}>
        {[0, 1, 2].map((i) => (
          <span key={i} className="block h-[3px] w-[3px] shrink-0 rounded-full bg-current" />
        ))}
      </span>
    </div>
  );
}

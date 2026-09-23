import React, { useEffect, useState } from "react";
import { createPortal } from "react-dom";
import { Menu, X } from "lucide-react";
import { cn } from "lib/utils";

/**
 * A navegação lateral em tela estreita, compartilhada pelos dois layouts.
 *
 * Existe um módulo só porque `AppLayout` e `AdminLayout` têm menus diferentes
 * mas o MESMO problema: uma barra fixa de 208–240 px num aparelho de 375 px
 * deixa o conteúdo sem espaço. Resolver nos dois lugares separadamente daria
 * duas gavetas com comportamentos ligeiramente diferentes — e quem usa os dois
 * painéis sentiria a diferença sem saber nomeá-la.
 */

/** Abaixo disto a barra vira gaveta. Igual ao `lg:` do Tailwind, para os
 *  utilitários `lg:` nos layouts concordarem com o que este hook decide. */
export const MOBILE_QUERY = "(max-width: 1023px)";

/**
 * True em telas estreitas.
 *
 * Usa `matchMedia` e não um listener de `resize`: o navegador só avisa quando a
 * resposta MUDA, em vez de disparar a cada pixel arrastado. Inicia lendo o
 * valor real, e não `false` — começar em `false` renderiza um quadro com o
 * layout de desktop antes de corrigir, e esse pisca aparece em todo carregamento
 * no celular.
 */
export function useIsMobile() {
  const [isMobile, setIsMobile] = useState(
    () => typeof window !== "undefined" && window.matchMedia(MOBILE_QUERY).matches
  );

  useEffect(() => {
    const mql = window.matchMedia(MOBILE_QUERY);
    const onChange = (e) => setIsMobile(e.matches);
    setIsMobile(mql.matches);
    mql.addEventListener("change", onChange);
    return () => mql.removeEventListener("change", onChange);
  }, []);

  return isMobile;
}

/**
 * Trava o scroll do body enquanto a gaveta está aberta.
 *
 * Sem isso, arrastar sobre o fundo escurecido rola a página atrás — a gaveta
 * fica parada sobre um conteúdo que se move, e ao fechar a pessoa está num
 * ponto da lista onde nunca pediu para estar.
 */
function useScrollLock(active) {
  useEffect(() => {
    if (!active) return undefined;
    const previous = document.body.style.overflow;
    document.body.style.overflow = "hidden";
    return () => { document.body.style.overflow = previous; };
  }, [active]);
}

/** Botão de abrir a gaveta. Só aparece em tela estreita (`lg:hidden`). */
export function MenuButton({ onClick, label }) {
  return (
    <button
      type="button"
      onClick={onClick}
      aria-label={label}
      className="lg:hidden inline-flex h-11 w-11 items-center justify-center rounded-lg text-muted-foreground hover:text-foreground hover:bg-muted transition-colors"
    >
      <Menu size={22} />
    </button>
  );
}

/**
 * A barra lateral: inline no desktop, gaveta sobreposta no celular.
 *
 * No desktop devolve o `<aside>` de sempre, com a largura que o layout pedir.
 * No celular vai para um portal no `body` — dentro da árvore, qualquer
 * ancestral com `overflow-hidden` (os dois layouts têm) recortaria a gaveta, e
 * ela apareceria cortada na altura do header.
 */
export function SidebarShell({
  isMobile, open, onClose, width = "w-52", closeLabel, children,
}) {
  useScrollLock(isMobile && open);

  // ESC fecha: a gaveta cobre a tela inteira e prende o foco visual, então
  // precisa da mesma saída que um modal.
  useEffect(() => {
    if (!isMobile || !open) return undefined;
    const onKey = (e) => { if (e.key === "Escape") onClose(); };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [isMobile, open, onClose]);

  if (!isMobile) {
    return (
      <aside
        className={cn(
          "relative bg-card border-r border-border flex-shrink-0",
          "transition-all duration-300 ease-in-out",
          width
        )}
      >
        {children}
      </aside>
    );
  }

  return createPortal(
    <>
      <div
        onClick={onClose}
        aria-hidden="true"
        className={cn(
          "fixed inset-0 z-[60] bg-black/60 transition-opacity duration-300",
          open ? "opacity-100" : "pointer-events-none opacity-0"
        )}
      />
      <aside
        role="dialog"
        aria-modal="true"
        className={cn(
          "fixed inset-y-0 left-0 z-[61] w-[min(18rem,85vw)] bg-card border-r border-border",
          "flex flex-col shadow-2xl transition-transform duration-300 ease-in-out",
          open ? "translate-x-0" : "-translate-x-full"
        )}
      >
        <div className="flex items-center justify-end border-b border-border p-2">
          <button
            type="button"
            onClick={onClose}
            aria-label={closeLabel}
            className="inline-flex h-11 w-11 items-center justify-center rounded-lg text-muted-foreground hover:text-foreground hover:bg-muted transition-colors"
          >
            <X size={22} />
          </button>
        </div>
        <div className="flex-1 overflow-y-auto scrollbar-thin">{children}</div>
      </aside>
    </>,
    document.body
  );
}

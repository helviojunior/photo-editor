import React from "react";
import { cn } from "lib/utils";

/** Liga/desliga com rótulo ao lado, no padrão do painel.
 *
 * Copiado de `../sec_chall/frontend/src/components/ui/toggle.jsx` — é o
 * controle booleano padrão de todo o sistema, no lugar de qualquer
 * `<input type="checkbox">`.
 *
 * Bloco de largura justa (`flex w-fit`), e não `inline-flex`: uma lista de
 * chaves empilhada é o uso normal, e como elemento inline ela grudava na
 * <Label> acima e ignorava o `space-y` do container — margem vertical não
 * vale em caixa inline. O `w-fit` mantém a área de clique do tamanho do
 * rótulo em vez de esticar pela linha inteira. */
export function Toggle({ checked, onChange, label, ariaLabel, disabled = false, className }) {
  return (
    <button type="button" disabled={disabled}
      role="switch" aria-checked={!!checked}
      aria-label={ariaLabel || (typeof label === "string" ? label : undefined)}
      onClick={() => !disabled && onChange(!checked)}
      className={cn("flex w-fit items-center gap-2 text-sm whitespace-nowrap",
        // O trilho tem 20px de altura: no dedo, o alvo sobe para 44 sem
        // mudar o desenho do controle (ver .touch-target em index.css).
        "touch-target",
        // O trilho tem 20px de altura: no dedo, o alvo sobe para 44 sem
        // mudar o desenho do controle (ver .touch-target em index.css).
        "touch-target",
        checked ? "text-foreground" : "text-muted-foreground hover:text-foreground",
        disabled && "opacity-50 cursor-not-allowed hover:text-muted-foreground",
        className)}>
      {/* Trilho 36x20 com bola de 16: sobram 2px de folga em cada lado, e é
          essa folga que o deslocamento tem de respeitar nas duas pontas —
          2px desligado, 18px ligado (36 - 16 - 2). */}
      <span className={cn(
        "relative inline-flex h-5 w-9 flex-shrink-0 items-center rounded-full transition-colors",
        checked ? "bg-brand-500" : "bg-muted")}>
        <span className={cn(
          "inline-block h-4 w-4 transform rounded-full bg-white transition-transform",
          checked ? "translate-x-[18px]" : "translate-x-0.5")} />
      </span>
      {label}
    </button>
  );
}

/**
 * Linha de booleano com título e texto de ajuda, em caixa: o rótulo (e a
 * descrição) à esquerda e o mesmo Toggle à direita. Use quando o campo precisa
 * explicar o que faz; para um toggle solto, use o <Toggle /> diretamente, que
 * já traz o rótulo ao lado.
 */
export function ToggleField({ checked, onChange, label, description, disabled }) {
  return (
    <div className="flex items-center justify-between rounded-lg border border-border p-4">
      <div className="space-y-0.5">
        <p className="text-sm font-medium leading-none">{label}</p>
        {description && (
          <p className="text-xs text-muted-foreground">{description}</p>
        )}
      </div>
      <Toggle checked={checked} onChange={onChange} disabled={disabled}
        ariaLabel={typeof label === "string" ? label : undefined} />
    </div>
  );
}

export default Toggle;

import React from "react";
import { Check } from "lucide-react";
import { cn } from "lib/utils";

/**
 * Marca de seleção de uma linha de listagem — círculo que recebe o ✓.
 *
 * Copiado do `../sec_chall` (`pages/admin/AdminUsers.js`), e é o padrão de
 * seleção em lista de todo o sistema.
 *
 * **Nenhuma listagem do baseline usa seleção ainda** — o componente está aqui
 * para que a primeira que precisar não invente outro controle.
 *
 * **Não é o `Toggle`.** O Toggle é para um estado que a linha *tem* ("conta
 * bloqueada", "exigir maiúscula") e que a pessoa liga e desliga. Isto aqui diz
 * "esta linha está no conjunto que vou operar agora" — outra pergunta. Usar o
 * mesmo controle para as duas fazia a lista parecer cheia de interruptores
 * pendurados em cada conta, como se cada um ligasse alguma coisa.
 *
 * É um `<span>`, e não um `<input>`: quem recebe o clique é a LINHA inteira
 * (área maior, e o mesmo alvo que abre a ficha quando não se está
 * selecionando). Por isso o estado vai em `aria-checked` no elemento da linha,
 * e não aqui.
 */
export function SelectionCheck({ checked, className }) {
  return (
    <span aria-hidden="true"
      className={cn(
        "flex h-5 w-5 items-center justify-center rounded-full border transition-colors",
        checked ? "border-primary bg-primary text-primary-foreground" : "border-border",
        className)}>
      {checked && <Check size={12} />}
    </span>
  );
}

export default SelectionCheck;

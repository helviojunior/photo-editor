import React from "react";
import { AlertTriangle } from "lucide-react";
import { cn } from "lib/utils";

/**
 * Mensagem de erro — sempre com fundo preenchido e ícone.
 *
 * O texto vermelho sozinho deixou de bastar quando o vermelho da marca passou
 * a ser a cor de acento do sistema: medida a distância perceptual, o vermelho
 * da logomarca (#ef3236) fica a ΔE 7,6 do `red-500` — abaixo de ~10 o olho
 * trata como a MESMA cor. Um "Confirmado" e um "não foi possível ler o
 * documento" ficariam indistinguíveis por cor.
 *
 * O que separa os dois aqui não é o tom, é a FORMA: erro tem caixa preenchida
 * e ícone de alerta; confirmação é texto solto em esmeralda. Isso também
 * resolve quem não distingue vermelho de verde — cerca de 8% dos homens —,
 * para quem a cor nunca foi sinal nenhum.
 *
 * O ícone é obrigatório de propósito: é ele que carrega o significado quando a
 * cor falha.
 */
export function FormError({ children, className }) {
  if (!children) return null;
  return (
    <div role="alert"
      className={cn(
        "flex items-start gap-2 rounded-md border border-red-500/40 bg-red-500/10",
        "px-3 py-2 text-xs text-red-400",
        className)}>
      <AlertTriangle className="h-3.5 w-3.5 mt-px flex-shrink-0" aria-hidden="true" />
      <span className="min-w-0">{children}</span>
    </div>
  );
}

/**
 * Vários erros de uma vez, na mesma caixa.
 *
 * Numa caixa só, e não uma por mensagem: três caixas empilhadas viram um bloco
 * vermelho que a pessoa deixa de ler.
 */
export function FormErrors({ items, className }) {
  const lista = (items || []).filter(Boolean);
  if (lista.length === 0) return null;
  return (
    <FormError className={className}>
      {lista.length === 1 ? lista[0] : (
        <span className="space-y-0.5">
          {lista.map((m) => <span key={m} className="block">{m}</span>)}
        </span>
      )}
    </FormError>
  );
}

export default FormError;

import React, { useEffect, useRef } from "react";
import { RotateCcw, Wand2 } from "lucide-react";
import { useI18n } from "i18n";
import { cn } from "lib/utils";
import { Button } from "components/ui/button";

const GROUPS = ["light", "color"];

function formatValue(slider, value) {
  const text = slider.step < 1 ? value.toFixed(2) : String(Math.round(value));
  return value > 0 ? `+${text}` : text;
}

/**
 * Painel de edição: Auto, Reset, presets e os sliders do motor.
 *
 * Os sliders vêm do backend (`/api/develop/`): limites, passo e grupo são do
 * motor, e o painel só os desenha. Arrastar muda o rascunho (`onDraft`, o
 * preview acompanha); soltar grava (`onCommit`) — uma entrada no histórico
 * por gesto, não uma por pixel arrastado. `onCommit()` sem argumento grava o
 * rascunho mais recente, que o Editor guarda num ref: no `pointerup` o
 * `draft` deste render pode ainda não ter o último `onChange`.
 */
export default function EditPanel({
  config, draft, onDraft, onCommit, onAuto, onReset, busy, disabled,
}) {
  const { t } = useI18n();
  // Seta segurada no slider = um ajuste, nao um por passo: grava quando o
  // teclado para por um instante.
  const keyTimer = useRef(null);
  useEffect(() => () => clearTimeout(keyTimer.current), []);
  if (!config || !draft) return null;

  const setValue = (name, value) =>
    onDraft({ ...draft, values: { ...draft.values, [name]: value } });

  // Soltar o slider grava e devolve o foco à página: com o foco no slider as
  // setas e o DEL seriam dele (TODO 5.6), e o atalho "morreria" até um clique.
  const commit = (e) => {
    onCommit();
    if (e?.pointerType && e.currentTarget) e.currentTarget.blur();
  };

  const resetSlider = (name) => {
    const next = { ...draft, values: { ...draft.values, [name]: 0 } };
    onDraft(next);
    onCommit(next);
  };

  const choosePreset = (id) => {
    const next = { ...draft, preset: draft.preset === id ? "" : id };
    onDraft(next);
    onCommit(next);
  };

  return (
    <div className={cn("space-y-4 p-4", disabled && "pointer-events-none opacity-60")}>
      <div className="grid grid-cols-2 gap-2">
        <Button variant="outline" size="sm" onClick={onAuto} loading={busy === "auto"}>
          {busy !== "auto" && <Wand2 className="h-4 w-4" />} {t("edit.auto", "Auto")}
        </Button>
        <Button variant="outline" size="sm" onClick={onReset} loading={busy === "reset"}>
          {busy !== "reset" && <RotateCcw className="h-4 w-4" />} {t("edit.reset", "Reset")}
        </Button>
      </div>

      <section>
        <h2 className="mb-2 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
          {t("edit.presets", "Presets")}
        </h2>
        <div className="flex flex-wrap gap-1.5">
          {config.presets.map((p) => {
            const active = draft.preset === p.id;
            return (
              <button
                key={p.id}
                type="button"
                onClick={() => choosePreset(p.id)}
                aria-pressed={active}
                className={cn(
                  "touch-target rounded-full border px-3 py-1 text-xs transition-colors",
                  active
                    ? "border-brand-400 bg-brand-400/15 text-brand-400"
                    : "border-border text-muted-foreground hover:text-foreground hover:bg-muted"
                )}
              >
                {t(`preset.${p.id}`, p.id)}
              </button>
            );
          })}
        </div>
      </section>

      {GROUPS.map((group) => (
        <section key={group}>
          <h2 className="mb-1 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
            {t(`edit.group.${group}`, group)}
          </h2>
          {config.sliders.filter((s) => s.group === group).map((s) => {
            const value = draft.values[s.name] ?? 0;
            const id = `slider-${s.name}`;
            return (
              <div key={s.name} className="py-1">
                <div className="flex items-baseline justify-between text-xs">
                  {/* Duplo clique no rótulo zera o controle, como no Lightroom. */}
                  <label htmlFor={id} onDoubleClick={() => resetSlider(s.name)}
                    title={t("edit.doubleClickReset", "Double-click to reset")}
                    className="cursor-default select-none">
                    {t(`adjust.${s.name}`, s.name)}
                  </label>
                  <span className={cn("tabular-nums", value ? "text-foreground" : "text-muted-foreground")}>
                    {formatValue(s, value)}
                  </span>
                </div>
                <input
                  id={id}
                  type="range"
                  min={s.min}
                  max={s.max}
                  step={s.step}
                  value={value}
                  onChange={(e) => setValue(s.name, Number(e.target.value))}
                  onPointerUp={commit}
                  onKeyUp={() => {
                    clearTimeout(keyTimer.current);
                    keyTimer.current = setTimeout(() => onCommit(), 600);
                  }}
                  className="touch-target w-full cursor-pointer accent-brand-500"
                />
              </div>
            );
          })}
        </section>
      ))}
    </div>
  );
}

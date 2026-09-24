import React from "react";
import { Brush, Check, Eraser, Layers, Pencil, Plus, Trash2, X } from "lucide-react";
import { useI18n } from "i18n";
import { cn } from "lib/utils";
import { Button } from "components/ui/button";
import { Toggle } from "components/ui/toggle";

export const BRUSH_MIN = 0.005;
export const BRUSH_MAX = 0.15;

const layerLabel = (tf, index) => tf("layers.name", { n: index + 1 });

/**
 * Camadas da foto: o "restante" (os ajustes da própria foto) e uma linha por
 * área extraída. A linha ativa é a que os sliders, os presets e o Auto
 * editam; clicar nela escolhe. Uma escolha só por vez, então as linhas são
 * um grupo de rádio, não o `SelectionCheck` (regra 2.3 do CLAUDE.md).
 */
export function LayersSection({
  layers, activeId, onActivate, onNew, onEdit, onDelete, max, disabled,
}) {
  const { t, tf } = useI18n();
  const full = layers.length >= max;
  const rows = [{ id: null, label: layers.length
    ? t("layers.rest", "Rest of the photo") : t("layers.whole", "Whole photo") },
  ...layers.map((l, i) => ({ id: l.id, label: layerLabel(tf, i) }))];

  return (
    <section>
      <div className="mb-1 flex items-center justify-between">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
          {t("layers.title", "Layers")}
        </h2>
        <Button variant="ghost" size="sm" onClick={onNew} disabled={disabled || full}
          title={full ? tf("layers.full", { max }) : t("layers.newShortcut", "Select an area (S)")}>
          <Plus className="h-4 w-4" /> {t("layers.new", "Select area")}
        </Button>
      </div>
      <div role="radiogroup" aria-label={t("layers.title", "Layers")}
        title={layers.length ? t("layers.cycle", "L switches to the next layer") : undefined}
        className="space-y-1">
        {rows.map((row) => {
          const active = row.id === activeId;
          return (
            <div key={row.id || "rest"}
              role="radio" aria-checked={active} tabIndex={0}
              onClick={() => onActivate(row.id)}
              onKeyDown={(e) => {
                if (e.key === "Enter" || e.key === " ") { e.preventDefault(); onActivate(row.id); }
              }}
              className={cn(
                "touch-target flex cursor-pointer items-center gap-2 rounded-md border px-2 py-1 text-xs transition-colors",
                active ? "border-brand-400 bg-brand-400/15 text-foreground"
                  : "border-border text-muted-foreground hover:bg-muted hover:text-foreground"
              )}>
              <Layers className={cn("h-3.5 w-3.5 shrink-0", active && "text-brand-400")} aria-hidden="true" />
              <span className="min-w-0 flex-1 truncate">{row.label}</span>
              {row.id && (
                <>
                  <LayerButton icon={Pencil} label={t("layers.editArea", "Edit area")}
                    onClick={() => onEdit(row.id)} />
                  <LayerButton icon={Trash2} label={t("layers.delete", "Delete layer")}
                    onClick={() => onDelete(row.id)} />
                </>
              )}
            </div>
          );
        })}
      </div>
    </section>
  );
}

function LayerButton({ icon: Icon, label, onClick }) {
  return (
    <button type="button" aria-label={label} title={label}
      onClick={(e) => { e.stopPropagation(); onClick(); }}
      className="touch-target rounded p-1 text-muted-foreground hover:bg-background/60 hover:text-foreground">
      <Icon className="h-3.5 w-3.5" />
    </button>
  );
}

/**
 * Controles do modo seleção (no lugar do painel de edição enquanto dura):
 * somar/subtrair, tamanho do pincel, detecção de objeto e concluir.
 */
export function SelectionPanel({
  editing, selection, brush, onBrush, smartAvailable, busy,
  onApply, onClear, onCancel,
}) {
  const { t, tf } = useI18n();
  const empty = !selection.mask;
  const percent = Math.round((selection.coverage || 0) * 1000) / 10;

  return (
    <div className="space-y-4 p-4">
      <section className="space-y-2">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
          {editing ? t("layers.editArea", "Edit area") : t("layers.new", "Select area")}
        </h2>
        <p className="text-[11px] text-muted-foreground">
          {t("layers.hint", "Paint over the object to extract it. Each stroke adds to the selection; Subtract (or holding Alt) removes. S or Enter finishes, Esc cancels.")}
        </p>
      </section>

      <div className="grid grid-cols-2 gap-2" role="group" aria-label={t("layers.brushMode", "Brush mode")}>
        {[["add", Brush, t("layers.add", "Add")], ["subtract", Eraser, t("layers.subtract", "Subtract")]]
          .map(([mode, Icon, label]) => (
            <Button key={mode} size="sm" variant={brush.mode === mode ? "default" : "outline"}
              aria-pressed={brush.mode === mode} onClick={() => onBrush({ mode })}>
              <Icon className="h-4 w-4" /> {label}
            </Button>
          ))}
      </div>

      <div>
        <div className="flex items-baseline justify-between text-xs">
          <label htmlFor="slider-brush-size">{t("layers.brushSize", "Brush size")}</label>
          <span className="tabular-nums text-muted-foreground">{Math.round(brush.size * 1000) / 10}%</span>
        </div>
        <input id="slider-brush-size" type="range" min={BRUSH_MIN} max={BRUSH_MAX} step={0.005}
          value={brush.size}
          onChange={(e) => onBrush({ size: Number(e.target.value) })}
          onPointerUp={(e) => e.currentTarget.blur()}
          className="touch-target w-full cursor-pointer accent-brand-500" />
      </div>

      <div className="space-y-1">
        <Toggle checked={brush.smart && smartAvailable} disabled={!smartAvailable}
          onChange={(v) => onBrush({ smart: v })}
          label={t("layers.smart", "Detect the object (AI)")} />
        <p className="text-[11px] text-muted-foreground">
          {smartAvailable
            ? t("layers.smartHint", "On: the stroke becomes the whole object under it. Off: exactly the painted area.")
            : t("layers.smartUnavailable", "The detection model is not installed; the brush selects exactly the painted area.")}
        </p>
      </div>

      <p className="text-xs text-muted-foreground" role="status">
        {busy ? t("layers.detecting", "Detecting…")
          : empty ? t("layers.emptySelection", "Nothing selected yet.")
            : tf("layers.coverage", { percent })}
      </p>

      <div className="grid grid-cols-2 gap-2">
        <Button size="sm" onClick={onApply} disabled={busy || (empty && !editing)}>
          <Check className="h-4 w-4" />
          {editing ? t("layers.apply", "Apply") : t("layers.create", "Create layer")}
        </Button>
        <Button size="sm" variant="outline" onClick={onClear} disabled={busy || empty}>
          <Eraser className="h-4 w-4" /> {t("layers.clear", "Clear")}
        </Button>
        <Button size="sm" variant="ghost" className="col-span-2" onClick={onCancel}>
          <X className="h-4 w-4" /> {t("common.cancel", "Cancel")}
        </Button>
      </div>
    </div>
  );
}

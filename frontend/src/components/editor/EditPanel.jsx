import React, { useEffect, useRef } from "react";
import { Check, Crop as CropIcon, RotateCcw, Wand2 } from "lucide-react";
import { useI18n } from "i18n";
import { cn } from "lib/utils";
import { Button } from "components/ui/button";
import { FormError } from "components/ui/form-error";
import { CROP_IDENTITY, feedOk, isCropIdentity, normalizeCrop } from "./crop";

const GROUPS = ["light", "color"];

function formatValue(slider, value) {
  const text = slider.step < 1 ? value.toFixed(2) : String(Math.round(value));
  return value > 0 ? `+${text}` : text;
}

/**
 * Painel de edição: Auto, Reset, crop, camadas, presets e os sliders do motor.
 *
 * Na versão Instagram (`instagram` = proporções do feed vindas do backend), o
 * crop ganha a escolha da proporção — 4:5, 1:1 ou 1,91:1 — e o giro só
 * endireita (±45°): o quadro nunca sai do que o Instagram aceita.
 *
 * Sliders e presets editam a camada ATIVA (`layerId`); sem camada ativa, os
 * ajustes da própria foto — que, havendo camadas, valem para o restante.
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
  cropMode, onToggleCrop, onCropChange, aspect, layerId = null, layersSection,
  instagram = null,
}) {
  const { t } = useI18n();
  // Seta segurada no slider = um ajuste, nao um por passo: grava quando o
  // teclado para por um instante.
  const keyTimer = useRef(null);
  useEffect(() => () => clearTimeout(keyTimer.current), []);
  if (!config || !draft) return null;

  const layer = layerId ? (draft.layers || []).find((l) => l.id === layerId) : null;
  const target = layer || draft;
  // O rascunho com `patch` ({values} e/ou {preset}) aplicado na camada ativa.
  const patched = (patch) => (layer
    ? { ...draft, layers: draft.layers.map((l) => (l.id === layer.id ? { ...l, ...patch } : l)) }
    : { ...draft, ...patch });

  const setValue = (name, value) =>
    onDraft(patched({ values: { ...target.values, [name]: value } }));

  // Soltar o slider grava e devolve o foco à página: com o foco no slider as
  // setas e o DEL seriam dele (TODO 5.6), e o atalho "morreria" até um clique.
  const commit = (e) => {
    onCommit();
    if (e?.pointerType && e.currentTarget) e.currentTarget.blur();
  };

  const resetSlider = (name) => {
    const next = patched({ values: { ...target.values, [name]: 0 } });
    onDraft(next);
    onCommit(next);
  };

  const ratioValues = instagram ? instagram.ratios : null;
  const maxAngle = instagram
    ? instagram.ratios.find((r) => r.ratio === draft.crop.ratio)?.max_angle ?? 45
    : config.crop.max_angle;
  // 1,91:1 girado além de 45°: o recorte sai vertical, fora do feed.
  const outOfFeed = !!instagram && !feedOk(draft.crop, aspect, instagram.feed_aspect);
  // Trocar a proporção recomeça o quadro no maior tamanho que cabe, no mesmo
  // centro e com o mesmo endireitamento.
  const chooseRatio = (ratio) => {
    onCropChange(normalizeCrop({ ...draft.crop, ratio, scale: 1 }, aspect, ratioValues), "ratio");
    onCommit();
  };

  const choosePreset = (id) => {
    const next = patched({ preset: target.preset === id ? "" : id });
    onDraft(next);
    onCommit(next);
  };

  return (
    <div className={cn("space-y-4 p-4", disabled && "pointer-events-none opacity-60")}>
      <div className="grid grid-cols-2 gap-2">
        <Button variant="outline" size="sm" onClick={onAuto} loading={busy === "auto"}
          title={t("edit.autoShortcut", "Auto (A)")}>
          {busy !== "auto" && <Wand2 className="h-4 w-4" />} {t("edit.auto", "Auto")}
        </Button>
        <Button variant="outline" size="sm" onClick={onReset} loading={busy === "reset"}>
          {busy !== "reset" && <RotateCcw className="h-4 w-4" />} {t("edit.reset", "Reset")}
        </Button>
      </div>

      <section>
        <Button variant={cropMode ? "default" : "outline"} size="sm" className="w-full"
          onClick={onToggleCrop} aria-pressed={!!cropMode}
          title={t("edit.cropShortcut", "Crop mode (C)")}>
          {cropMode ? <Check className="h-4 w-4" /> : <CropIcon className="h-4 w-4" />}
          {cropMode ? t("edit.cropDone", "Done") : t("edit.crop", "Crop")}
        </Button>
        {instagram && (
          <div className="mt-2">
            <div className="mb-1 text-xs">{t("instagram.ratio", "Instagram format")}</div>
            <div className="grid grid-cols-3 gap-1.5" role="group"
              aria-label={t("instagram.ratio", "Instagram format")}>
              {instagram.ratios.map((r) => {
                const active = draft.crop.ratio === r.ratio;
                return (
                  <button key={r.id} type="button" onClick={() => chooseRatio(r.ratio)}
                    aria-pressed={active}
                    title={t(`instagram.ratio.${r.id}`, r.id)}
                    className={cn(
                      "touch-target flex flex-col items-center gap-1 rounded-md border px-2 py-1.5 text-xs transition-colors",
                      active
                        ? "border-brand-400 bg-brand-400/15 text-brand-400"
                        : "border-border text-muted-foreground hover:text-foreground hover:bg-muted"
                    )}>
                    {/* O desenho da proporção, para reconhecer sem ler. */}
                    <span aria-hidden="true" className="block rounded-[2px] border-2 border-current"
                      style={{ width: 14 / Math.max(1, r.ratio), height: 14 * Math.min(1, r.ratio) }} />
                    {r.id}
                  </button>
                );
              })}
            </div>
          </div>
        )}
        {cropMode && (
          <div className="mt-2 space-y-2">
            <p className="text-[11px] text-muted-foreground">
              {instagram
                ? t("instagram.cropHint",
                  "Drag the frame to move it, the corners to resize, outside it to rotate (up to 45°; 90° in 1.91:1, like the normal crop; ← → 15° at a time). The frame keeps the Instagram format chosen above. C or Esc exits.")
                : t("edit.cropHint", "Drag the frame to move it, the corners to resize, outside it to rotate.")}
            </p>
            <div>
              <div className="flex items-baseline justify-between text-xs">
                <label htmlFor="slider-crop-angle">{t("edit.angle", "Angle")}</label>
                <span className="tabular-nums">{draft.crop.angle.toFixed(1)}°</span>
              </div>
              <input
                id="slider-crop-angle"
                type="range"
                min={-maxAngle}
                max={maxAngle}
                step={0.1}
                value={draft.crop.angle}
                onChange={(e) => onCropChange(
                  normalizeCrop({ ...draft.crop, angle: Number(e.target.value) }, aspect, ratioValues),
                  "rotate")}
                onPointerUp={commit}
                onKeyUp={() => {
                  clearTimeout(keyTimer.current);
                  keyTimer.current = setTimeout(() => onCommit(), 600);
                }}
                className="touch-target w-full cursor-pointer accent-brand-500"
              />
            </div>
            {outOfFeed && (
              <FormError>
                {t("instagram.outOfFeed",
                  "Past 45° the 1.91:1 frame becomes vertical (1:1.91), outside the formats the feed accepts: it is exported, but cannot be published.")}
              </FormError>
            )}
            <Button variant="ghost" size="sm" className="w-full"
              disabled={instagram
                ? draft.crop.scale >= 0.9999 && draft.crop.angle === 0
                  && draft.crop.cx === 0.5 && draft.crop.cy === 0.5
                : isCropIdentity(draft.crop)}
              onClick={() => {
                // Na versão Instagram o quadro volta ao maior da proporção
                // escolhida — "sem crop" não existe nela.
                onCropChange(instagram
                  ? normalizeCrop({ ratio: draft.crop.ratio }, aspect, ratioValues)
                  : { ...CROP_IDENTITY }, "reset");
                onCommit();
              }}>
              <RotateCcw className="h-4 w-4" /> {t("edit.cropReset", "Reset crop")}
            </Button>
          </div>
        )}
      </section>

      {layersSection}

      <section>
        <h2 className="mb-2 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
          {t("edit.presets", "Presets")}
        </h2>
        <div className="flex flex-wrap gap-1.5">
          {config.presets.map((p) => {
            const active = target.preset === p.id;
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
            const value = target.values[s.name] ?? 0;
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

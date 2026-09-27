import React, { useRef } from "react";
import { Brush, Image as ImageIcon, Pencil, Trash2 } from "lucide-react";
import { useI18n } from "i18n";
import { cn } from "lib/utils";
import { Button } from "components/ui/button";
import { Toggle } from "components/ui/toggle";
import { FormError } from "components/ui/form-error";
import useFitSize from "./useFitSize";

/** URL do recorte de uma camada para uma máscara (a gravada ou a da seleção). */
export const cutoutUrl = (layer, mask) => `${layer.cutout_url}&mask=${mask}`;

// Mesmos extremos do backend (services/merges.py: FIRST/LAST_OPACITY).
const FAINT = 0.25;
const STRONG = 1;

/**
 * Opacidade de cada foto, da base à última camada: "fadeIn" (o padrão) deixa
 * o objeto da base bem claro e cada foto seguinte mais forte; "fadeOut" faz
 * o contrário; "solid" deixa todas opacas.
 */
export function rampOpacities(kind, n) {
  if (kind === "solid") return Array(n).fill(1);
  const step = n > 1 ? (STRONG - FAINT) / (n - 1) : 0;
  const values = Array.from({ length: n }, (_, i) => Math.round((FAINT + i * step) * 100) / 100);
  return kind === "fadeOut" ? values.reverse() : values;
}

const RAMPS = [
  ["fadeIn", "Fade in"],
  ["fadeOut", "Fade out"],
  ["solid", "Solid"],
];

/**
 * Resultado do merge: o preview da base e, empilhados por cima, os PNGs das
 * áreas já alinhadas — primeiro o fundo por trás do objeto da base (com
 * opacidade 1 − a do objeto), depois as camadas em ordem. Todos têm o
 * tamanho do preview da base e ocupam a mesma caixa, então cada pixel cai no
 * lugar certo; a transparência é o `opacity` do próprio `<img>`.
 */
export function CompositePane({ label, base, pieces }) {
  const paneRef = useRef(null);
  const aspect = base?.width && base?.height ? base.height / base.width : 2 / 3;
  const size = useFitSize(paneRef, aspect);
  return (
    <figure ref={paneRef}
      className="relative flex min-h-0 min-w-0 items-center justify-center overflow-hidden bg-neutral-900">
      <figcaption className="absolute left-2 top-2 z-20 rounded bg-black/60 px-2 py-0.5 text-[11px] font-medium uppercase tracking-wide text-white/90">
        {label}
      </figcaption>
      {base && (
        <div style={{ width: size.w, height: size.h }} className="relative select-none">
          <img src={base.preview_url} alt={base.file_name} draggable={false}
            className="pointer-events-none absolute inset-0 h-full w-full" />
          {pieces.map((p) => (
            <img key={p.key} src={p.src} alt="" aria-hidden="true" draggable={false}
              style={{ opacity: p.opacity }}
              className="pointer-events-none absolute inset-0 h-full w-full" />
          ))}
        </div>
      )}
    </figure>
  );
}

const fmt = (value, digits) => Number(value).toLocaleString(undefined,
  { minimumFractionDigits: digits, maximumFractionDigits: digits });

/**
 * Camadas do merge: uma linha por foto de origem, com a área, a análise de
 * alinhamento, a visibilidade e a opacidade, e a base (a foto inteira,
 * embaixo), onde a área é o objeto DELA, que a opacidade esmaece. A linha
 * ativa é a que aparece à esquerda (e recebe o pincel); uma por vez, então
 * as linhas são um grupo de rádio.
 */
export default function MergePanel({
  merge, activeId, onActivate, onSelectArea, onPatch, onRamp, onRemove,
}) {
  const { t } = useI18n();

  return (
    <div className="space-y-4 p-4">
      <section className="space-y-2">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
          {t("merge.opacityRamp", "Transparency")}
        </h2>
        <div className="grid grid-cols-3 gap-2">
          {RAMPS.map(([kind, fallback]) => (
            <Button key={kind} size="sm" variant="outline" onClick={() => onRamp(kind)}>
              {t(`merge.ramp.${kind}`, fallback)}
            </Button>
          ))}
        </div>
      </section>

      <section className="space-y-1">
        <h2 className="text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
          {t("merge.layers", "Layers")}
        </h2>
        <p className="pb-1 text-[11px] text-muted-foreground">
          {t("merge.layersHint", "Paint the area to extract from each photo (S). Only the painted area goes into the result; the rest of the photo is used to align it to the base.")}
        </p>
        <div role="radiogroup" aria-label={t("merge.layers", "Layers")} className="space-y-2">
          {/* De cima para baixo: a última camada fica por cima de todas. */}
          {[...merge.layers].reverse().map((layer) => (
            <LayerRow key={layer.photo.id} layer={layer} active={layer.photo.id === activeId}
              onActivate={() => onActivate(layer.photo.id)}
              onSelectArea={() => onSelectArea(layer.photo.id)}
              onPatch={(patch, now) => onPatch({ [layer.photo.id]: patch }, now)}
              onRemove={() => onRemove(layer.photo)} />
          ))}
          <LayerRow isBase active={merge.base.id === activeId}
            layer={{ ...merge.base_layer, photo: merge.base }}
            onActivate={() => onActivate(merge.base.id)}
            onSelectArea={() => onSelectArea(merge.base.id)}
            onPatch={(patch, now) => onPatch({ [merge.base.id]: patch }, now)} />
        </div>
      </section>
    </div>
  );
}

function LayerRow({ layer, active, onActivate, onSelectArea, onPatch, onRemove, isBase = false }) {
  const { t, tf } = useI18n();
  const a = layer.align;
  const percent = Math.round(layer.opacity * 100);
  const inputId = `merge-opacity-${layer.photo.id}`;
  return (
    <div className={cn("rounded-md border", active ? "border-brand-400" : "border-border")}>
      <Row active={active} onActivate={onActivate} photo={layer.photo} bare
        detail={isBase
          ? (layer.mask ? t("merge.baseHasArea", "Base · object selected")
            : t("merge.baseNoArea", "Base · select its object to fade it"))
          : (layer.mask ? t("merge.hasArea", "Area selected")
            : t("merge.noArea", "No area selected"))}
        missing={!layer.mask && !isBase} />
      <div className="space-y-2 px-2 pb-2">
        {isBase ? null : !layer.photo.active ? (
          <FormError>{t("merge.photoGone", "This photo is no longer in raw/; the layer is skipped.")}</FormError>
        ) : a.ok ? (
          <p className="text-[11px] tabular-nums text-muted-foreground"
            title={tf("merge.alignDetail", { inliers: a.inliers, error: fmt(a.error ?? 0, 2) })}>
            {tf("merge.align", {
              dx: fmt(a.dx, 1), dy: fmt(a.dy, 1), angle: fmt(a.angle, 2),
              scale: fmt(a.scale * 100, 2),
            })}
          </p>
        ) : (
          <FormError>{t("merge.alignFailed", "Could not align this photo to the base; it is placed as is.")}</FormError>
        )}
        <div className="flex items-center gap-2">
          <Button size="sm" variant={layer.mask ? "outline" : "default"} className="flex-1"
            onClick={onSelectArea} disabled={!layer.photo.active}>
            {layer.mask ? <Pencil className="h-4 w-4" /> : <Brush className="h-4 w-4" />}
            {layer.mask ? t("merge.editArea", "Edit area") : t("merge.selectArea", "Select area")}
          </Button>
          {!isBase && (
            <>
              <Toggle checked={layer.visible} onChange={(v) => onPatch({ visible: v }, true)}
                label={t("merge.visible", "Visible")} />
              <Button size="sm" variant="ghost" onClick={onRemove}
                aria-label={t("merge.remove", "Remove from the merge")}
                title={t("merge.remove", "Remove from the merge")}>
                <Trash2 className="h-4 w-4" />
              </Button>
            </>
          )}
        </div>
        <div>
          <div className="flex items-baseline justify-between text-xs">
            <label htmlFor={inputId}>
              {isBase ? t("merge.baseOpacity", "Object opacity") : t("merge.opacity", "Opacity")}
            </label>
            <span className="tabular-nums text-muted-foreground">{percent}%</span>
          </div>
          <input id={inputId} type="range" min={0} max={1} step={0.01} value={layer.opacity}
            disabled={isBase && !layer.mask}
            onChange={(e) => onPatch({ opacity: Number(e.target.value) })}
            onPointerUp={(e) => e.currentTarget.blur()}
            className="touch-target w-full cursor-pointer accent-brand-500" />
        </div>
      </div>
    </div>
  );
}

function Row({ photo, detail, active, onActivate, bare = false, missing = false }) {
  return (
    <div role="radio" aria-checked={active} tabIndex={0}
      onClick={onActivate}
      onKeyDown={(e) => {
        if (e.key === "Enter" || e.key === " ") { e.preventDefault(); onActivate(); }
      }}
      className={cn(
        "touch-target flex cursor-pointer items-center gap-2 p-2 text-xs transition-colors",
        bare ? "rounded-t-md" : "rounded-md border",
        !bare && (active ? "border-brand-400" : "border-border"),
        active ? "bg-brand-400/15 text-foreground" : "text-muted-foreground hover:bg-muted hover:text-foreground"
      )}>
      {photo.thumbnail_url ? (
        <img src={photo.thumbnail_url} alt="" aria-hidden="true" draggable={false}
          className="h-10 w-14 shrink-0 rounded-sm object-cover" />
      ) : (
        <ImageIcon className="h-4 w-4 shrink-0" aria-hidden="true" />
      )}
      <div className="min-w-0 flex-1">
        <div className="truncate font-medium text-foreground">{photo.file_name}</div>
        <div className={cn("truncate text-[11px]", missing && "text-amber-400")}>{detail}</div>
      </div>
    </div>
  );
}

import React, { useEffect, useRef } from "react";
import { cn } from "lib/utils";
import { useI18n } from "i18n";
import { SelectionCheck } from "components/ui/selection-check";

/**
 * Faixa de thumbnails: a foto em edição fica SEMPRE ao centro, as anteriores à
 * esquerda e as seguintes à direita.
 *
 * Os espaçadores de meia largura nas pontas existem para que a primeira e a
 * última foto também possam chegar ao centro — sem eles o scroll para no
 * limite e a foto atual fica encostada na borda.
 *
 * Com `picked` (lista de ids), a faixa está no modo de marcar fotos (o
 * merge): clicar marca/desmarca em vez de abrir, cada foto ganha o círculo
 * de seleção (regra 2.3) e a primeira marcada, na ordem da faixa, leva o
 * selo de base.
 */
export default function Filmstrip({ photos, currentId, onSelect, picked = null, onPick }) {
  const { t } = useI18n();
  const stripRef = useRef(null);
  const picking = Array.isArray(picked);
  const baseId = picking ? photos.find((p) => picked.includes(p.id))?.id : null;

  useEffect(() => {
    const strip = stripRef.current;
    const el = strip?.querySelector(`[data-photo-id="${currentId}"]`);
    if (!el) return;
    // Centraliza pela conta, não por scrollIntoView: este rola também os
    // ancestrais verticais e, no celular, arrastava a página inteira.
    const left = el.offsetLeft - (strip.clientWidth - el.offsetWidth) / 2;
    strip.scrollTo({ left, behavior: "smooth" });
  }, [currentId, photos]);

  return (
    <div
      ref={stripRef}
      className="flex h-full min-h-0 items-stretch gap-2 overflow-x-auto overflow-y-hidden py-2 scrollbar-thin"
    >
      <div className="w-1/2 shrink-0" aria-hidden="true" />
      {photos.map((photo) => {
        const active = photo.id === currentId;
        const checked = picking && picked.includes(photo.id);
        return (
          <button
            key={photo.id}
            type="button"
            data-photo-id={photo.id}
            onClick={() => (picking ? onPick(photo.id) : onSelect(photo.id))}
            title={photo.file_name}
            role={picking ? "checkbox" : undefined}
            aria-checked={picking ? checked : undefined}
            aria-current={active ? "true" : undefined}
            style={{ aspectRatio: `${photo.width || 3} / ${photo.height || 2}` }}
            className={cn(
              "relative h-full shrink-0 overflow-hidden rounded-sm bg-neutral-800 transition",
              active || checked
                ? "ring-2 ring-brand-400 ring-offset-2 ring-offset-card"
                : "opacity-60 hover:opacity-100"
            )}
          >
            {picking && (
              <SelectionCheck checked={checked}
                className="absolute left-1.5 top-1.5 z-10 bg-black/50 text-white" />
            )}
            {!picking && (photo.merge_id || photo.copy_of) && (
              <span className="absolute bottom-1 left-1 z-10 rounded bg-black/70 px-1.5 py-0.5 text-[10px] font-semibold uppercase text-white">
                {photo.merge_id ? t("merge.badge", "Merge") : t("editor.copyBadge", "Copy")}
              </span>
            )}
            {photo.id === baseId && (
              <span className="absolute bottom-1 left-1 z-10 rounded bg-brand-500 px-1.5 py-0.5 text-[10px] font-semibold uppercase text-white">
                {t("merge.base", "Base")}
              </span>
            )}
            <img
              src={photo.thumbnail_url}
              alt={photo.file_name}
              loading="lazy"
              draggable={false}
              className="h-full w-full object-cover"
            />
          </button>
        );
      })}
      <div className="w-1/2 shrink-0" aria-hidden="true" />
    </div>
  );
}

import React, { useEffect, useRef } from "react";
import { cn } from "lib/utils";

/**
 * Faixa de thumbnails: a foto em edição fica SEMPRE ao centro, as anteriores à
 * esquerda e as seguintes à direita.
 *
 * Os espaçadores de meia largura nas pontas existem para que a primeira e a
 * última foto também possam chegar ao centro — sem eles o scroll para no
 * limite e a foto atual fica encostada na borda.
 */
export default function Filmstrip({ photos, currentId, onSelect }) {
  const stripRef = useRef(null);

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
        return (
          <button
            key={photo.id}
            type="button"
            data-photo-id={photo.id}
            onClick={() => onSelect(photo.id)}
            title={photo.file_name}
            aria-current={active ? "true" : undefined}
            style={{ aspectRatio: `${photo.width || 3} / ${photo.height || 2}` }}
            className={cn(
              "relative h-full shrink-0 overflow-hidden rounded-sm bg-neutral-800 transition",
              active
                ? "ring-2 ring-brand-400 ring-offset-2 ring-offset-card"
                : "opacity-60 hover:opacity-100"
            )}
          >
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

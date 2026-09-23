import React from "react";
import { cn } from "lib/utils";

/**
 * Um lado do antes/depois: rótulo no topo e a foto inteira visível
 * (`object-contain`), sobre fundo neutro — como no Develop do Lightroom.
 */
export default function ImagePane({ label, src, alt, busy = false, className }) {
  return (
    <figure className={cn("relative flex min-h-0 min-w-0 flex-col bg-neutral-900", className)}>
      <figcaption className="absolute left-2 top-2 z-10 rounded bg-black/60 px-2 py-0.5 text-[11px] font-medium uppercase tracking-wide text-white/90">
        {label}
      </figcaption>
      <div className="flex min-h-0 flex-1 items-center justify-center p-2">
        {src && (
          <img
            src={src}
            alt={alt}
            draggable={false}
            className={cn(
              "max-h-full max-w-full select-none object-contain transition-opacity",
              busy && "opacity-80"
            )}
          />
        )}
      </div>
    </figure>
  );
}

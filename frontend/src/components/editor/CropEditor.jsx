import React, { useEffect, useRef, useState } from "react";
import { cn } from "lib/utils";
import { normalizeCrop } from "./crop";

// Cantos em "L" (a alça visível) dentro de uma área de toque maior e
// invisível; (sx, sy) diz qual canto é, nos eixos do quadro.
const CORNERS = [
  [-1, -1, "-left-3 -top-3", "cursor-nwse-resize", "left-2.5 top-2.5 border-l-[3px] border-t-[3px]"],
  [1, -1, "-right-3 -top-3", "cursor-nesw-resize", "right-2.5 top-2.5 border-r-[3px] border-t-[3px]"],
  [-1, 1, "-left-3 -bottom-3", "cursor-nesw-resize", "left-2.5 bottom-2.5 border-l-[3px] border-b-[3px]"],
  [1, 1, "-right-3 -bottom-3", "cursor-nwse-resize", "right-2.5 bottom-2.5 border-r-[3px] border-b-[3px]"],
];

/**
 * Modo crop, sobre a foto ORIGINAL (painel da esquerda): a foto fica PARADA e
 * só o quadro se mexe. O painel da direita segue mostrando o resultado, já
 * recortado, enquanto o quadro é arrastado.
 *
 *   arrastar dentro do quadro ...... move
 *   arrastar um canto .............. redimensiona com o canto oposto fixo
 *                                    (sempre na proporção da foto)
 *   arrastar fora do quadro ........ gira o quadro em torno do centro dele
 *   ← / → (no Editor) .............. gira de 15 em 15 graus
 *
 * `onChange(crop, mode)` diz o gesto: o Editor usa isso para o giro não
 * encolher o quadro de vez (ver `updateCrop`).
 *
 * O quadro é um div girado por CSS sobre a imagem; a sombra de 9999px dele,
 * recortada pelo `overflow-hidden` da moldura, escurece o que fica de fora.
 * Toda mudança passa por `normalizeCrop`, que mantém o quadro dentro da foto.
 */
export default function CropEditor({ src, busy, photo, crop, onChange, onCommit, label }) {
  const paneRef = useRef(null);
  const frameRef = useRef(null);
  const drag = useRef(null);
  const [size, setSize] = useState({ w: 0, h: 0 });
  const aspect = photo.width && photo.height ? photo.height / photo.width : 1;

  // Maior retângulo na proporção da foto que cabe no painel.
  useEffect(() => {
    const pane = paneRef.current;
    if (!pane) return undefined;
    const fit = () => {
      const pw = pane.clientWidth - 16;
      const ph = pane.clientHeight - 16;
      const w = Math.max(Math.min(pw, ph / aspect), 0);
      setSize({ w, h: w * aspect });
    };
    fit();
    const ro = new ResizeObserver(fit);
    ro.observe(pane);
    return () => ro.disconnect();
  }, [aspect]);

  const center = () => {
    const r = frameRef.current.getBoundingClientRect();
    return [r.left + crop.cx * size.w, r.top + crop.cy * size.h];
  };

  // (sx, sy) = canto arrastado, em -1/+1 nos eixos do quadro.
  const start = (mode, sx = 0, sy = 0) => (e) => {
    if (e.button !== undefined && e.button !== 0) return;
    e.preventDefault();
    e.stopPropagation();
    frameRef.current.setPointerCapture(e.pointerId);
    drag.current = { mode, sx, sy, x: e.clientX, y: e.clientY, crop, c: center(),
      r: frameRef.current.getBoundingClientRect() };
  };

  // Canto oposto FIXO (como no Lightroom): o ponteiro, levado para os eixos
  // do quadro girado, define a nova largura; a altura segue a proporção.
  const resize = (d, e) => {
    const { crop: c0, sx, sy, r } = d;
    const t = (c0.angle * Math.PI) / 180;
    const cos = Math.cos(t);
    const sin = Math.sin(t);
    const hw = (c0.scale * size.w) / 2;
    const hh = (c0.scale * size.h) / 2;
    const [cx, cy] = d.c;
    const ox = cx - cos * sx * hw + sin * sy * hh;
    const oy = cy - sin * sx * hw - cos * sy * hh;
    const dx = e.clientX - ox;
    const dy = e.clientY - oy;
    const u = cos * dx + sin * dy;
    const v = -sin * dx + cos * dy;
    const w = Math.max(sx * u, sy * v * (size.w / size.h), 1);
    const h = (w * size.h) / size.w;
    const ncx = ox + cos * (sx * w) / 2 - sin * (sy * h) / 2;
    const ncy = oy + sin * (sx * w) / 2 + cos * (sy * h) / 2;
    return { ...c0, scale: w / size.w, cx: (ncx - r.left) / size.w, cy: (ncy - r.top) / size.h };
  };

  const move = (e) => {
    const d = drag.current;
    if (!d || !size.w) return;
    const [ccx, ccy] = d.c;
    let next;
    if (d.mode === "move") {
      next = { ...d.crop, cx: d.crop.cx + (e.clientX - d.x) / size.w,
        cy: d.crop.cy + (e.clientY - d.y) / size.h };
    } else if (d.mode === "resize") {
      next = resize(d, e);
    } else {
      const a0 = Math.atan2(d.y - ccy, d.x - ccx);
      const a1 = Math.atan2(e.clientY - ccy, e.clientX - ccx);
      next = { ...d.crop, angle: d.crop.angle + ((a1 - a0) * 180) / Math.PI };
    }
    onChange(normalizeCrop(next, aspect), d.mode);
  };

  const end = () => {
    if (!drag.current) return;
    drag.current = null;
    onCommit();
  };

  return (
    <figure ref={paneRef}
      className="relative flex min-h-0 min-w-0 items-center justify-center overflow-hidden bg-neutral-900">
      <figcaption className="absolute left-2 top-2 z-20 rounded bg-black/60 px-2 py-0.5 text-[11px] font-medium uppercase tracking-wide text-white/90">
        {label}
      </figcaption>
      <div
        ref={frameRef}
        onPointerDown={start("rotate")}
        onPointerMove={move}
        onPointerUp={end}
        onPointerCancel={end}
        style={{ width: size.w, height: size.h }}
        className="relative cursor-grab touch-none select-none overflow-hidden"
      >
        {src && (
          <img src={src} alt="" draggable={false}
            className={cn("pointer-events-none absolute inset-0 h-full w-full transition-opacity",
              busy && "opacity-80")} />
        )}
        <div
          onPointerDown={start("move")}
          style={{
            left: crop.cx * size.w,
            top: crop.cy * size.h,
            width: crop.scale * size.w,
            height: crop.scale * size.h,
            transform: `translate(-50%, -50%) rotate(${crop.angle}deg)`,
            boxShadow: "0 0 0 9999px rgba(0, 0, 0, 0.55)",
          }}
          className="absolute cursor-move border-2 border-brand-400"
        >
          {/* Regra dos terços */}
          <div className="pointer-events-none absolute inset-y-0 left-1/3 w-px bg-white/40" />
          <div className="pointer-events-none absolute inset-y-0 left-2/3 w-px bg-white/40" />
          <div className="pointer-events-none absolute inset-x-0 top-1/3 h-px bg-white/40" />
          <div className="pointer-events-none absolute inset-x-0 top-2/3 h-px bg-white/40" />
          {CORNERS.map(([sx, sy, pos, cursor, bracket]) => (
            <div key={pos} onPointerDown={start("resize", sx, sy)}
              className={cn("absolute h-9 w-9", pos, cursor)}>
              <span className={cn("pointer-events-none absolute h-4 w-4 border-white", bracket)} />
            </div>
          ))}
        </div>
      </div>
    </figure>
  );
}

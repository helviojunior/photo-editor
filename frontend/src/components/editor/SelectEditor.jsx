import React, { useEffect, useRef, useState } from "react";
import { Loader2 } from "lucide-react";
import { cn } from "lib/utils";
import useFitSize from "./useFitSize";

export const maskUrl = (key) => (key ? `/api/masks/${key}.png` : null);

/**
 * Máscara pintada por cima da foto: um bloco da cor de acento recortado pelo
 * PNG da máscara (`mask-image` usa o alfa dele). A cor fica no tema, não no
 * arquivo.
 */
export function MaskOverlay({ mask, className }) {
  const url = maskUrl(mask);
  if (!url) return null;
  const style = {
    WebkitMaskImage: `url(${url})`, maskImage: `url(${url})`,
    WebkitMaskSize: "100% 100%", maskSize: "100% 100%",
  };
  return <div aria-hidden="true" style={style}
    className={cn("pointer-events-none absolute inset-0 bg-brand-500", className)} />;
}

/**
 * Seleção por pincel, sobre a foto ORIGINAL (painel da esquerda), como o
 * crop. A pessoa pinta por cima do objeto e, ao soltar, o traço vai para o
 * backend (`onStroke`), que devolve a máscara do objeto sob ele; a seleção
 * aparece na cor de acento. Cada traço soma (ou, no modo subtrair ou com Alt,
 * tira) da seleção que já existe.
 *
 * Sem `onStroke`, só mostra a foto com a máscara — é o que o painel da
 * esquerda exibe quando uma camada está ativa, para se ver o que ela cobre.
 *
 * Coordenadas: frações da foto inteira (antes do crop); o raio do pincel é
 * fração do lado maior, então o traço tem o mesmo tamanho em qualquer tela.
 */
export default function SelectEditor({
  src, photo, mask, label, onStroke, brush, busy = false, faint = false,
}) {
  const paneRef = useRef(null);
  const frameRef = useRef(null);
  const canvasRef = useRef(null);
  const stroke = useRef(null);
  const [cursor, setCursor] = useState(null);
  const aspect = photo.width && photo.height ? photo.height / photo.width : 1;
  const size = useFitSize(paneRef, aspect);
  const editable = !!onStroke;
  const radiusPx = (brush?.size || 0) * Math.max(size.w, size.h);

  // O canvas guarda o traço desenhado até a máscara nova chegar.
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !size.w) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(size.w * dpr);
    canvas.height = Math.round(size.h * dpr);
    canvas.getContext("2d").setTransform(dpr, 0, 0, dpr, 0, 0);
  }, [size.w, size.h]);

  // Terminou tudo o que estava na fila: o traço já virou máscara.
  useEffect(() => {
    if (busy) return;
    const canvas = canvasRef.current;
    if (canvas) canvas.getContext("2d").clearRect(0, 0, canvas.width, canvas.height);
  }, [busy, mask]);

  const local = (e) => {
    const r = frameRef.current.getBoundingClientRect();
    return [e.clientX - r.left, e.clientY - r.top];
  };

  const draw = (from, to, mode) => {
    const ctx = canvasRef.current.getContext("2d");
    // A cor vem do tema: o canvas herda `color` das classes dele.
    const color = getComputedStyle(canvasRef.current).color;
    ctx.strokeStyle = mode === "subtract" ? "rgba(255, 255, 255, 0.6)" : color;
    ctx.fillStyle = ctx.strokeStyle;
    ctx.lineWidth = radiusPx * 2;
    ctx.lineCap = "round";
    ctx.lineJoin = "round";
    ctx.beginPath();
    if (from) {
      ctx.moveTo(from[0], from[1]);
      ctx.lineTo(to[0], to[1]);
      ctx.stroke();
    } else {
      ctx.arc(to[0], to[1], radiusPx, 0, Math.PI * 2);
      ctx.fill();
    }
  };

  const down = (e) => {
    if (!editable || (e.button !== undefined && e.button !== 0) || !size.w) return;
    e.preventDefault();
    frameRef.current.setPointerCapture(e.pointerId);
    const mode = e.altKey ? "subtract" : brush.mode;
    const p = local(e);
    stroke.current = { mode, points: [p] };
    draw(null, p, mode);
  };

  const move = (e) => {
    if (!editable) return;
    const p = local(e);
    setCursor(p);
    const s = stroke.current;
    if (!s) return;
    const last = s.points[s.points.length - 1];
    if (Math.hypot(p[0] - last[0], p[1] - last[1]) < 3) return;
    draw(last, p, s.mode);
    s.points.push(p);
  };

  const up = () => {
    const s = stroke.current;
    stroke.current = null;
    if (!s) return;
    onStroke({
      mode: s.mode,
      radius: brush.size,
      points: s.points.map(([x, y]) => [x / size.w, y / size.h]),
    });
  };

  return (
    <figure ref={paneRef}
      className="relative flex min-h-0 min-w-0 items-center justify-center overflow-hidden bg-neutral-900">
      <figcaption className="absolute left-2 top-2 z-20 rounded bg-black/60 px-2 py-0.5 text-[11px] font-medium uppercase tracking-wide text-white/90">
        {label}
      </figcaption>
      {busy && (
        <Loader2 aria-hidden="true"
          className="absolute right-2 top-2 z-20 h-4 w-4 animate-spin text-white/90" />
      )}
      <div
        ref={frameRef}
        onPointerDown={down}
        onPointerMove={move}
        onPointerUp={up}
        onPointerCancel={up}
        onPointerLeave={() => setCursor(null)}
        style={{ width: size.w, height: size.h }}
        className={cn("relative select-none overflow-hidden",
          editable && "cursor-none touch-none")}
      >
        {src && (
          <img src={src} alt="" draggable={false}
            className="pointer-events-none absolute inset-0 h-full w-full" />
        )}
        <MaskOverlay mask={mask} className={faint ? "opacity-35" : "opacity-55"} />
        {editable && (
          <canvas ref={canvasRef} aria-hidden="true"
            className="pointer-events-none absolute inset-0 h-full w-full text-brand-500 opacity-60" />
        )}
        {editable && cursor && (
          <div aria-hidden="true"
            style={{ left: cursor[0] - radiusPx, top: cursor[1] - radiusPx,
              width: radiusPx * 2, height: radiusPx * 2 }}
            className="pointer-events-none absolute rounded-full border-2 border-white shadow-[0_0_0_1px_rgba(0,0,0,0.6)]" />
        )}
      </div>
    </figure>
  );
}

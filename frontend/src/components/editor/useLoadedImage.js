import { useEffect, useState } from "react";

/**
 * Troca a imagem só depois que a nova terminou de carregar: enquanto o slider
 * é arrastado, a foto editada continua na tela (a anterior) em vez de piscar
 * vazia a cada render do backend.
 */
export default function useLoadedImage(url) {
  const [shown, setShown] = useState(url);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!url) { setShown(null); return undefined; }
    let alive = true;
    setLoading(true);
    const img = new Image();
    img.onload = () => { if (alive) { setShown(url); setLoading(false); } };
    img.onerror = () => { if (alive) setLoading(false); };
    img.src = url;
    return () => { alive = false; };
  }, [url]);

  return { src: shown, loading };
}

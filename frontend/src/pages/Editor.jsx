import React, { useCallback, useEffect, useMemo, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";
import { ImageOff, RefreshCw } from "lucide-react";
import api from "lib/api";
import { useI18n } from "i18n";
import { Button } from "components/ui/button";
import { FormError } from "components/ui/form-error";
import ImagePane from "components/editor/ImagePane";
import Filmstrip from "components/editor/Filmstrip";

/**
 * Tela do editor (estilo Develop do Lightroom), uma rota por foto.
 *
 *   70% superior: original | editada | painel de edição
 *   30% inferior: filmstrip com a foto atual ao centro
 *
 * Abaixo de `lg:` as duas fotos ficam lado a lado numa faixa, o painel desce
 * para baixo delas e a página rola — no celular não há altura para 70/30.
 */
export default function Editor() {
  const { id } = useParams();
  const navigate = useNavigate();
  const { t, tf } = useI18n();

  const [photos, setPhotos] = useState(null);
  const [loadError, setLoadError] = useState(false);
  const [rescanning, setRescanning] = useState(false);
  const [status, setStatus] = useState("");

  const loadPhotos = useCallback(async () => {
    try {
      const res = await api.get("/api/photos/");
      setPhotos(res.data.results);
      setLoadError(false);
      return res.data.results;
    } catch {
      setLoadError(true);
      return null;
    }
  }, []);

  useEffect(() => { loadPhotos(); }, [loadPhotos]);

  const index = useMemo(
    () => (photos ? photos.findIndex((p) => p.id === id) : -1),
    [photos, id]
  );
  const current = index >= 0 ? photos[index] : null;

  const goTo = useCallback(
    (photoId, replace = false) => navigate(`/photos/${photoId}`, { replace }),
    [navigate]
  );

  // Sem foto na URL (ou com uma que saiu do catálogo): abre a primeira.
  useEffect(() => {
    if (photos && photos.length && index < 0) goTo(photos[0].id, true);
  }, [photos, index, goTo]);

  const rescan = async () => {
    setRescanning(true);
    try {
      const res = await api.post("/api/photos/rescan/");
      await loadPhotos();
      setStatus(tf("editor.rescanDone", { total: res.data.total }));
    } catch {
      setStatus(t("editor.rescanError"));
    } finally {
      setRescanning(false);
    }
  };

  if (loadError) {
    return (
      <div className="w-full p-4 lg:p-6">
        <FormError>{t("editor.loadError")}</FormError>
      </div>
    );
  }

  if (photos && photos.length === 0) {
    return (
      <div className="flex h-full w-full flex-col items-center justify-center gap-3 p-6 text-center">
        <ImageOff className="h-10 w-10 text-muted-foreground" aria-hidden="true" />
        <h1 className="text-lg font-semibold">{t("editor.empty")}</h1>
        <p className="text-sm text-muted-foreground">{t("editor.emptyHint")}</p>
        <Button variant="outline" onClick={rescan} loading={rescanning}>
          <RefreshCw className="h-4 w-4" /> {t("editor.rescan")}
        </Button>
      </div>
    );
  }

  return (
    <div className="flex w-full flex-col lg:h-full">
      {/* Parte superior: 70% da altura no desktop */}
      <section className="flex flex-col border-b border-border lg:h-[70%] lg:flex-row">
        <div className="grid h-[42vh] min-h-0 grid-cols-2 gap-px bg-border lg:h-auto lg:flex-1">
          <ImagePane label={t("editor.original")} src={current?.preview_url}
            alt={current?.file_name} />
          <ImagePane label={t("editor.edited")} src={current?.preview_url}
            alt={current?.file_name} />
        </div>

        <aside className="w-full border-t border-border bg-card lg:w-72 lg:shrink-0 lg:overflow-y-auto lg:border-l lg:border-t-0 scrollbar-thin">
          {current && (
            <div className="space-y-1 border-b border-border p-4 text-xs">
              <div className="truncate text-sm font-semibold" title={current.file_name}>
                {current.file_name}
              </div>
              <div className="text-muted-foreground">
                {current.width} × {current.height}
                {current.captured_at && (
                  <> · {new Date(current.captured_at).toLocaleString()}</>
                )}
              </div>
            </div>
          )}
        </aside>
      </section>

      {/* Parte inferior: filmstrip (30%) */}
      <section className="flex h-44 flex-col bg-card lg:h-[30%]">
        <div className="flex min-h-11 flex-wrap items-center gap-x-4 gap-y-1 border-b border-border px-3 py-1 text-xs">
          <span className="font-medium">
            {photos && index >= 0
              ? tf("editor.counter", { position: index + 1, total: photos.length })
              : t("common.loading")}
          </span>
          {status && <span className="text-muted-foreground" role="status">{status}</span>}
          <div className="ml-auto flex items-center gap-2">
            <Button variant="ghost" size="sm" onClick={rescan} loading={rescanning}>
              {!rescanning && <RefreshCw className="h-4 w-4" />}
              <span className="hidden sm:inline">{t("editor.rescan")}</span>
            </Button>
          </div>
        </div>
        <div className="min-h-0 flex-1 px-2">
          {photos && (
            <Filmstrip photos={photos} currentId={current?.id} onSelect={goTo} />
          )}
        </div>
      </section>
    </div>
  );
}

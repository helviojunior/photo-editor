import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";
import { ChevronLeft, ChevronRight, ImageOff, RefreshCw, Trash2, Undo2, Upload } from "lucide-react";
import api from "lib/api";
import { useI18n } from "i18n";
import { useDialog } from "contexts/DialogContext";
import { Button } from "components/ui/button";
import { FormError } from "components/ui/form-error";
import ImagePane from "components/editor/ImagePane";
import CropEditor from "components/editor/CropEditor";
import Filmstrip from "components/editor/Filmstrip";
import PhotoHistory from "components/editor/PhotoHistory";
import EditPanel from "components/editor/EditPanel";
import ExportDialog from "components/editor/ExportDialog";
import useShortcuts from "components/editor/useShortcuts";
import useLoadedImage from "components/editor/useLoadedImage";
import renderUrl, { sameState } from "components/editor/renderUrl";
import { normalizeCrop } from "components/editor/crop";
import { SORT_OPTIONS, readSort, sortPhotos, writeSort } from "components/editor/sortPhotos";

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
  const { alert } = useDialog();

  // Catálogo como veio da API; `photos` é ele na ordem escolhida na barra.
  const [catalog, setPhotos] = useState(null);
  const [sort, setSort] = useState(readSort);
  const photos = useMemo(() => sortPhotos(catalog, sort), [catalog, sort]);
  const changeSort = (value) => { setSort(value); writeSort(value); };
  const [loadError, setLoadError] = useState(false);
  const [rescanning, setRescanning] = useState(false);
  const [status, setStatus] = useState("");
  // Sobe a cada acao: o historico da foto recarrega.
  const [historyVersion, setHistoryVersion] = useState(0);
  // Uma acao por vez: DEL segurado nao pode disparar varias exclusoes.
  const busyRef = useRef(false);
  const [busy, setBusy] = useState("");

  // Edicao: config do motor, rascunho dos ajustes (o que o slider mostra) e a
  // chave que manda o rascunho voltar ao que esta gravado.
  const [develop, setDevelop] = useState(null);
  const [draft, setDraft] = useState(null);
  const draftRef = useRef(null);
  const [syncKey, setSyncKey] = useState(0);
  const [previewUrl, setPreviewUrl] = useState(null);

  // Exportacao: estado vindo do backend e se o modal esta aberto.
  const [exportStatus, setExportStatus] = useState(null);
  const [exportOpen, setExportOpen] = useState(false);

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
  useEffect(() => {
    api.get("/api/develop/").then((res) => setDevelop(res.data)).catch(() => {});
    // Uma exportacao pode estar rodando desde antes de a pagina abrir.
    api.get("/api/export/").then((res) => setExportStatus(res.data)).catch(() => {});
  }, []);

  // Enquanto exporta, acompanha o progresso.
  const exporting = !!exportStatus?.running;
  useEffect(() => {
    if (!exporting) return undefined;
    const timer = setInterval(() => {
      api.get("/api/export/").then((res) => setExportStatus(res.data)).catch(() => {});
    }, 800);
    return () => clearInterval(timer);
  }, [exporting]);

  const startExport = async () => {
    try {
      if (!exporting) {
        const res = await api.post("/api/export/");
        setExportStatus(res.data);
      }
      setExportOpen(true);
    } catch (err) {
      await alert({
        title: t("export.startError", "Could not start the export"),
        description: err?.response?.data?.error || t("error.generic"),
        variant: "danger",
      });
    }
  };

  const index = useMemo(
    () => (photos ? photos.findIndex((p) => p.id === id) : -1),
    [photos, id]
  );
  const current = index >= 0 ? photos[index] : null;

  const goTo = useCallback(
    (photoId, replace = false) => navigate(`/photos/${photoId}`, { replace }),
    [navigate]
  );

  // Posição da última foto aberta: se ela sai da lista (excluída), a vizinha
  // que ocupou o lugar dela é a próxima — e, se era a última, a anterior.
  const lastIndexRef = useRef(0);
  useEffect(() => { if (index >= 0) lastIndexRef.current = index; }, [index]);

  // Sem foto na URL, ou com uma que saiu do catálogo: abre a da mesma posição.
  useEffect(() => {
    if (photos && photos.length && index < 0) {
      goTo(photos[Math.min(lastIndexRef.current, photos.length - 1)].id, true);
    }
  }, [photos, index, goTo]);

  // Troca de foto (ou Auto/Reset/desfazer): o rascunho vira o estado gravado.
  const currentId = current?.id;
  useEffect(() => {
    const saved = current?.adjustments || null;
    draftRef.current = saved;
    setDraft(saved);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [currentId, syncKey]);

  const updateDraft = useCallback((next) => {
    draftRef.current = next;
    setDraft(next);
  }, []);

  // Modo crop: o quadro vai sobre a ORIGINAL (esquerda) e a editada (direita)
  // mostra o recorte ao vivo. Sai ao trocar de foto e com ESC.
  const [cropMode, setCropMode] = useState(false);
  useEffect(() => { setCropMode(false); }, [currentId]);
  useEffect(() => {
    if (!cropMode) return undefined;
    const onKey = (e) => {
      if (e.key === "Escape" && !document.querySelector('[role="dialog"]')) setCropMode(false);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [cropMode]);
  // Girar encolhe o quadro para ele caber na foto; sem memória, girar e
  // voltar a 0° deixaria o quadro pequeno. `baseScale` é o tamanho que a
  // pessoa escolheu (ao entrar no modo ou redimensionando), e todo giro parte
  // dele — o quadro só encolhe o quanto o ângulo exige.
  const baseScaleRef = useRef(1);
  useEffect(() => {
    if (cropMode && draftRef.current) baseScaleRef.current = draftRef.current.crop.scale;
  }, [cropMode]);
  const aspect = current?.width ? current.height / current.width : 1;

  const updateCrop = useCallback((crop, mode) => {
    let next = crop;
    if (mode === "rotate") {
      next = normalizeCrop({ ...crop, scale: baseScaleRef.current }, aspect);
    } else {
      baseScaleRef.current = crop.scale;
    }
    updateDraft({ ...draftRef.current, crop: next });
  }, [updateDraft, aspect]);

  // ← / → no modo crop: gira o quadro para o próximo múltiplo de 15°.
  const CROP_STEP = 15;
  const rotateCrop = useCallback((dir) => {
    const crop = draftRef.current?.crop;
    if (!crop) return;
    const k = crop.angle / CROP_STEP;
    const target = (dir > 0 ? Math.floor(k + 1e-6) + 1 : Math.ceil(k - 1e-6) - 1) * CROP_STEP;
    updateCrop({ ...crop, angle: target }, "rotate");
  }, [updateCrop]);

  // O render acompanha o rascunho com um respiro curto: arrastar o slider
  // pede uma imagem a cada pausa, nao uma por evento.
  useEffect(() => {
    const url = renderUrl(current, draft);
    const timer = setTimeout(() => setPreviewUrl(url), 120);
    return () => clearTimeout(timer);
  }, [current, draft]);
  const edited = useLoadedImage(previewUrl);

  const replacePhoto = useCallback((photo) => {
    setPhotos((list) => list.map((p) => (p.id === photo.id ? photo : p)));
  }, []);

  const runAction = useCallback(async (fn, name = "action") => {
    if (busyRef.current) return;
    busyRef.current = true;
    setBusy(name);
    try {
      await fn();
    } catch (err) {
      await alert({
        title: t("editor.actionError", "Could not complete the action"),
        description: err?.response?.data?.error || t("error.generic"),
        variant: "danger",
      });
    } finally {
      busyRef.current = false;
      setBusy("");
      setHistoryVersion((v) => v + 1);
    }
  }, [alert, t]);

  // Ultimo estado GRAVADO da foto atual. Atualizado na resposta de cada PUT,
  // sem esperar o render: o proximo da fila ja compara com ele.
  const savedRef = useRef({ id: null, state: null });
  useEffect(() => {
    savedRef.current = { id: current?.id, state: current?.adjustments };
  }, [current]);

  // Gravacoes em FILA, uma por vez: em paralelo elas chegavam fora de ordem e
  // o banco ficava com um valor intermediario do slider.
  const saveQueue = useRef(Promise.resolve());

  // Soltou o slider / escolheu preset: grava se mudou algo.
  const commitDraft = useCallback((next) => {
    const state = next || draftRef.current;
    const photoId = current?.id;
    if (!photoId || !state) return;
    saveQueue.current = saveQueue.current.then(async () => {
      const saved = savedRef.current;
      if (saved.id === photoId && sameState(state, saved.state)) return;
      try {
        const res = await api.put(`/api/photos/${photoId}/adjustments/`, state);
        if (savedRef.current.id === photoId) {
          savedRef.current = { id: photoId, state: res.data.adjustments };
        }
        replacePhoto(res.data);
      } catch (err) {
        await alert({
          title: t("edit.saveError", "Could not save the adjustment"),
          description: err?.response?.data?.error || t("error.generic"),
          variant: "danger",
        });
      } finally {
        setHistoryVersion((v) => v + 1);
      }
    });
  }, [current, replacePhoto, alert, t]);

  // Soltar o slider FORA dele nao dispara o pointerup do slider: a janela
  // pega esse caso. Gravar e idempotente (sem mudanca, nao grava).
  const commitRef = useRef(commitDraft);
  commitRef.current = commitDraft;
  useEffect(() => {
    const onUp = () => setTimeout(() => commitRef.current(), 0);
    window.addEventListener("pointerup", onUp);
    return () => window.removeEventListener("pointerup", onUp);
  }, []);

  const autoOrReset = useCallback((kind) => runAction(async () => {
    if (!current) return;
    const res = await api.post(`/api/photos/${current.id}/${kind}/`);
    replacePhoto(res.data);
    setSyncKey((k) => k + 1);
  }, kind), [runAction, current, replacePhoto]);

  const step = useCallback((delta) => {
    if (!photos || index < 0) return;
    const target = photos[index + delta];
    if (target) goTo(target.id);
  }, [photos, index, goTo]);

  // DEL: move para deleted/. Tirar a foto da lista basta para seguir para a
  // próxima: o efeito acima abre a que ficou na mesma posição.
  const deleteCurrent = useCallback(() => runAction(async () => {
    if (!current) return;
    await api.delete(`/api/photos/${current.id}/`);
    setPhotos((list) => list.filter((p) => p.id !== current.id));
    setStatus(tf("editor.deleted", { name: current.file_name }));
  }), [runAction, current, tf]);

  // CTRL/CMD+Z: desfaz a ultima acao de QUALQUER foto e abre a foto afetada.
  const undo = useCallback(() => runAction(async () => {
    const res = await api.post("/api/history/undo/");
    const { undone, photo } = res.data;
    if (!undone) {
      setStatus(t("editor.nothingToUndo", "Nothing to undo."));
      return;
    }
    setStatus(tf("editor.undone", { action: t(`action.${undone.kind}`, undone.kind) }));
    const list = await loadPhotos();
    setSyncKey((k) => k + 1);
    if (photo && list?.some((p) => p.id === photo.id)) goTo(photo.id);
  }), [runAction, t, tf, loadPhotos, goTo]);

  // No modo crop as setas giram o quadro (e cada toque grava); fora dele,
  // trocam de foto. C entra e sai do modo crop; A aplica o Auto.
  useShortcuts({
    onNext: () => (cropMode ? (rotateCrop(1), commitDraft()) : step(1)),
    onPrev: () => (cropMode ? (rotateCrop(-1), commitDraft()) : step(-1)),
    onDelete: deleteCurrent,
    onUndo: undo,
    onCrop: () => { if (current) setCropMode((m) => !m); },
    onAuto: () => autoOrReset("auto"),
  });

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
          {cropMode && current && draft ? (
            <CropEditor label={t("editor.original")} src={current.preview_url}
              photo={current} crop={draft.crop} onChange={updateCrop}
              onCommit={() => commitDraft()} />
          ) : (
            <ImagePane label={t("editor.original")} src={current?.preview_url}
              alt={current?.file_name} />
          )}
          <ImagePane label={t("editor.edited")} src={edited.src}
            alt={current?.file_name} busy={edited.loading} />
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
          <EditPanel config={develop} draft={draft} onDraft={updateDraft}
            onCommit={commitDraft} onAuto={() => autoOrReset("auto")}
            onReset={() => autoOrReset("reset")} busy={busy} disabled={!current}
            cropMode={cropMode} onToggleCrop={() => setCropMode((m) => !m)}
            onCropChange={updateCrop}
            aspect={aspect} />
          <div className="border-t border-border">
            <PhotoHistory photoId={current?.id} version={historyVersion} />
          </div>
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
          <select
            value={sort}
            onChange={(e) => changeSort(e.target.value)}
            aria-label={t("editor.sort", "Sort photos")}
            className="touch-target h-8 rounded-md border border-border bg-transparent px-2 text-xs text-muted-foreground hover:text-foreground focus:outline-none cursor-pointer"
          >
            {SORT_OPTIONS.map((o) => (
              <option key={o} value={o} className="bg-card text-foreground">
                {t(`editor.sort.${o}`, o)}
              </option>
            ))}
          </select>
          <div className="ml-auto flex items-center gap-1">
            {/* Os mesmos comandos dos atalhos, para quem nao tem teclado. */}
            <IconButton icon={ChevronLeft} label={t("editor.prev", "Previous photo (←)")}
              onClick={() => step(-1)} disabled={index <= 0} />
            <IconButton icon={ChevronRight} label={t("editor.next", "Next photo (→)")}
              onClick={() => step(1)} disabled={!photos || index >= photos.length - 1} />
            <IconButton icon={Trash2} label={t("editor.delete", "Delete photo (Del)")}
              onClick={deleteCurrent} disabled={!current} />
            <IconButton icon={Undo2} label={t("editor.undo", "Undo (Ctrl/Cmd+Z)")}
              onClick={undo} />
            <Button variant="ghost" size="sm" onClick={rescan} loading={rescanning}>
              {!rescanning && <RefreshCw className="h-4 w-4" />}
              <span className="hidden sm:inline">{t("editor.rescan")}</span>
            </Button>
            <Button size="sm" onClick={startExport} loading={exporting}>
              {!exporting && <Upload className="h-4 w-4" />}
              {exporting
                ? tf("export.progress", { done: exportStatus.done, total: exportStatus.total })
                : t("export.button", "Export")}
            </Button>
          </div>
        </div>
        <div className="min-h-0 flex-1 px-2">
          {photos && (
            <Filmstrip photos={photos} currentId={current?.id} onSelect={goTo} />
          )}
        </div>
      </section>

      <ExportDialog open={exportOpen} status={exportStatus}
        onClose={() => setExportOpen(false)} />
    </div>
  );
}

function IconButton({ icon: Icon, label, ...props }) {
  return (
    <Button variant="ghost" size="sm" aria-label={label} title={label} {...props}>
      <Icon className="h-4 w-4" />
    </Button>
  );
}

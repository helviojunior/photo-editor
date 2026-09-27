import React, { useCallback, useEffect, useRef, useState } from "react";
import { useNavigate, useParams } from "react-router-dom";
import { ArrowLeft, Unlink } from "lucide-react";
import api from "lib/api";
import { useI18n } from "i18n";
import { useDialog } from "contexts/DialogContext";
import { Button } from "components/ui/button";
import { FormError } from "components/ui/form-error";
import ImagePane from "components/editor/ImagePane";
import SelectEditor from "components/editor/SelectEditor";
import { SelectionPanel } from "components/editor/LayersPanel";
import MergePanel, { CompositePane, cutoutUrl, rampOpacities } from "components/editor/MergePanel";
import useBrushSelection from "components/editor/useBrushSelection";

// Um pouco mais que a bola na tela: o pincel do editor nasce grande (pessoas).
const BRUSH_SIZE = 0.015;
const SAVE_DELAY = 350;

// A camada de uma foto do merge; a da base é o objeto dela (`base_layer`).
const layerOf = (m, photoId) => {
  if (!m || !photoId) return null;
  if (photoId === m.base.id) return { ...m.base_layer, photo: m.base, visible: true };
  return m.layers.find((l) => l.photo.id === photoId) || null;
};

/**
 * Tela do merge (`/merges/:id`): áreas de várias fotos, alinhadas, em camadas
 * sobre a foto base — a trajetória da bola a partir de uma sequência.
 *
 *   esquerda: a foto da camada ativa (com o pincel, no modo seleção)
 *   direita:  o resultado — a base e, por cima, a área de cada camada
 *
 * A base também tem área: o objeto DELA (a bola da primeira foto), que a
 * opacidade esmaece — o que aparece por trás vem de uma foto seguinte, já
 * alinhada, onde o objeto não está mais ali.
 *   painel:   camadas e transparência
 *
 * O backend já alinhou cada foto à base pelo fundo ao criar o merge, e
 * realinha sem a área quando ela muda. A área de cada camada chega como um
 * PNG alinhado sobre o preview da base, então mudar a transparência é só CSS.
 *
 * Tudo é gravado sozinho, e nada vai para raw/: no editor, a base passa a
 * mostrar o merge na "Editada" e o Exportar grava `<base>_merge.jpg`. As
 * fotos das camadas ficam fora da filmstrip até o merge ser desfeito.
 */
export default function Merge() {
  const { id } = useParams();
  const navigate = useNavigate();
  const { t, tf } = useI18n();
  const { alert, confirm } = useDialog();

  const [merge, setMerge] = useState(null);
  const mergeRef = useRef(null);
  const [loadError, setLoadError] = useState(false);
  const [smartAvailable, setSmartAvailable] = useState(false);
  const [active, setActive] = useState(null);
  const [selecting, setSelecting] = useState(false);
  const [leaving, setLeaving] = useState(false);
  const {
    selection, selectionRef, brush, setBrush, stepBrush, busy: segmenting,
    request: segmentRequest, reset: resetSelection, setSelection,
  } = useBrushSelection(BRUSH_SIZE);

  const applyMerge = useCallback((next) => { mergeRef.current = next; setMerge(next); }, []);

  useEffect(() => {
    api.get(`/api/merges/${id}/`).then((res) => {
      applyMerge(res.data);
      setLoadError(false);
      // Começa pela primeira camada que ainda não tem área.
      const first = res.data.layers.find((l) => !l.mask) || res.data.layers[0];
      setActive(first?.photo.id || null);
    }).catch(() => setLoadError(true));
    api.get("/api/develop/").then((res) => setSmartAvailable(!!res.data?.layers?.smart_select))
      .catch(() => {});
  }, [id, applyMerge]);

  // Gravações: mudanças por camada se acumulam e vão num PUT só, em fila.
  // A resposta só substitui o estado local se nada mudou depois do envio —
  // senão ela desfaria o slider que ainda está sendo arrastado.
  const pending = useRef({});
  const timer = useRef(null);
  const queue = useRef(Promise.resolve());
  const edits = useRef(0);

  const flush = useCallback(() => {
    clearTimeout(timer.current);
    const items = Object.entries(pending.current).map(([photo, patch]) => ({ photo, ...patch }));
    pending.current = {};
    if (!items.length) return queue.current;
    queue.current = queue.current.then(async () => {
      const sent = edits.current;
      try {
        const res = await api.put(`/api/merges/${id}/`, { layers: items });
        if (sent === edits.current) applyMerge(res.data);
        else {
          // Mantém o que é local; do servidor, só a análise nova.
          const byPhoto = Object.fromEntries(res.data.layers.map((l) => [l.photo.id, l]));
          const cur = mergeRef.current;
          applyMerge({ ...cur,
            base_layer: { ...cur.base_layer, cutout_url: res.data.base_layer.cutout_url },
            layers: cur.layers.map((l) => (
              byPhoto[l.photo.id] ? { ...l, align: byPhoto[l.photo.id].align,
                cutout_url: byPhoto[l.photo.id].cutout_url } : l)) });
        }
      } catch (err) {
        await alert({
          title: t("merge.saveLayersError", "Could not save the layers"),
          description: err?.response?.data?.error || t("error.generic"),
          variant: "danger",
        });
      }
    });
    return queue.current;
  }, [id, alert, t, applyMerge]);

  const patchLayers = useCallback((patches, now = false) => {
    const m = mergeRef.current;
    if (!m) return;
    edits.current += 1;
    applyMerge({ ...m,
      base_layer: { ...m.base_layer, ...patches[m.base.id] },
      layers: m.layers.map((l) => (
        patches[l.photo.id] ? { ...l, ...patches[l.photo.id] } : l)) });
    Object.entries(patches).forEach(([photo, patch]) => {
      pending.current[photo] = { ...pending.current[photo], ...patch };
    });
    clearTimeout(timer.current);
    if (now) flush();
    else timer.current = setTimeout(flush, SAVE_DELAY);
  }, [applyMerge, flush]);

  useEffect(() => () => { flush(); }, [flush]);

  const activeLayer = layerOf(merge, active);

  // Modo seleção: o pincel sobre a foto da camada ativa, partindo da área
  // que ela já tem. Aplicar grava na hora (o backend realinha sem a área).
  const startSelect = useCallback((photoId) => {
    const layer = layerOf(mergeRef.current, photoId);
    if (!layer || !layer.photo.active) return;
    setActive(photoId);
    resetSelection(layer.mask || null);
    setSelecting(true);
    segmentRequest(photoId, {});
  }, [resetSelection, segmentRequest]);

  const exitSelect = useCallback(() => {
    resetSelection();
    setSelecting(false);
  }, [resetSelection]);

  const applySelection = useCallback(() => {
    if (!selecting || !active || segmenting) return;
    const mask = selectionRef.current.mask || "";
    exitSelect();
    patchLayers({ [active]: { mask: mask || null } }, true);
  }, [selecting, active, segmenting, selectionRef, exitSelect, patchLayers]);

  const addStroke = useCallback((stroke) => {
    if (active) segmentRequest(active, { stroke, smart: brush.smart });
  }, [active, brush.smart, segmentRequest]);

  // S seleciona a área da camada ativa; no modo seleção, Enter (ou S)
  // conclui, Esc cancela e [ ] mudam o pincel — como no editor.
  useEffect(() => {
    const onKey = (e) => {
      if (document.querySelector('[role="dialog"]') || e.ctrlKey || e.metaKey || e.altKey) return;
      const tag = e.target?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return;
      const key = e.key.toLowerCase();
      if (!selecting) {
        if (key === "s" && active) { e.preventDefault(); startSelect(active); }
        return;
      }
      if (e.key === "Escape") exitSelect();
      else if ((e.key === "Enter" && tag !== "BUTTON") || key === "s") { e.preventDefault(); applySelection(); }
      else if (e.key === "[" || e.key === "]") { e.preventDefault(); stepBrush(e.key === "]" ? 1 : -1); }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [selecting, active, startSelect, exitSelect, applySelection, stepBrush]);

  // A rampa vai da base (o objeto dela) à última camada.
  const setRamp = (kind) => {
    const m = mergeRef.current;
    if (!m) return;
    const ids = [m.base.id, ...m.layers.map((l) => l.photo.id)];
    const values = rampOpacities(kind, ids.length);
    patchLayers(Object.fromEntries(ids.map((photoId, i) => [photoId, { opacity: values[i] }])));
  };

  // Volta ao editor na base, depois de gravar o que falta: a "Editada" de
  // lá já tem de mostrar o merge como ficou.
  const backToEditor = async () => {
    if (!mergeRef.current) return;
    setLeaving(true);
    await flush();
    navigate(`/photos/${mergeRef.current.base.id}`);
  };

  // Tira a foto da composição (ela volta para a filmstrip). Na última
  // camada, o backend desfaz o merge inteiro.
  const removeLayer = async (photo) => {
    const last = (mergeRef.current?.layers.length || 0) <= 1;
    await confirm({
      title: t("merge.removeTitle", "Remove from the merge?"),
      description: last
        ? tf("merge.removeLastDescription", { name: photo.file_name })
        : tf("merge.removeDescription", { name: photo.file_name }),
      variant: "danger",
      confirmLabel: t("merge.removeConfirm", "Remove"),
      onConfirm: async () => {
        await flush();
        const res = await api.delete(`/api/merges/${id}/layers/${photo.id}/`);
        if (res.data.dissolved) {
          navigate(`/photos/${mergeRef.current.base.id}`);
          return;
        }
        edits.current += 1;
        applyMerge(res.data.merge);
        if (active === photo.id) setActive(res.data.merge.base.id);
      },
    });
  };

  const dissolve = async () => {
    if (!merge) return;
    await confirm({
      title: t("merge.dissolveTitle", "Undo the merge?"),
      description: tf("merge.dissolveDescription", { count: merge.layers.length }),
      variant: "danger",
      confirmLabel: t("merge.dissolve", "Undo merge"),
      onConfirm: async () => {
        clearTimeout(timer.current);
        pending.current = {};
        await api.delete(`/api/merges/${id}/`);
        navigate(`/photos/${merge.base.id}`);
      },
    });
  };

  if (loadError) {
    return (
      <div className="w-full p-4 lg:p-6">
        <FormError>{t("merge.loadError", "Could not load the merge.")}</FormError>
      </div>
    );
  }

  // O que o resultado mostra: o fundo por trás do objeto da base (quanto
  // mais claro o objeto, mais forte o fundo), a área gravada de cada camada
  // visível e, na camada que está sendo selecionada, a seleção em construção.
  const maskOf = (l) => (selecting && l.photo.id === active ? selection.mask : l.mask);
  const baseLayer = layerOf(merge, merge?.base.id);
  const baseMask = baseLayer && maskOf(baseLayer);
  const pieces = [
    baseMask && baseLayer.opacity < 1
      ? { key: "base", src: cutoutUrl(baseLayer, baseMask), opacity: 1 - baseLayer.opacity } : null,
    ...(merge?.layers || []).map((l) => {
      const mask = maskOf(l);
      return l.visible && mask ? { key: l.photo.id, src: cutoutUrl(l, mask), opacity: l.opacity } : null;
    }),
  ].filter(Boolean);
  const readyCount = (merge?.layers || []).filter((l) => l.visible && l.mask).length;

  return (
    <div className="flex w-full flex-col lg:h-full">
      <div className="flex min-h-11 flex-wrap items-center gap-2 border-b border-border bg-card px-3 py-1 text-xs">
        <Button variant="ghost" size="sm" onClick={backToEditor} disabled={!merge}
          loading={leaving}>
          <ArrowLeft className="h-4 w-4" /> {t("merge.back", "Editor")}
        </Button>
        <h1 className="min-w-0 truncate text-sm font-semibold">
          {merge ? tf("merge.title", { name: merge.base.file_name }) : t("common.loading")}
        </h1>
        {merge && (
          <span className="hidden text-muted-foreground md:inline">
            {readyCount
              ? tf("merge.exportHint", { name: merge.export_name })
              : t("merge.noAreasHint", "Select the area of at least one layer")}
          </span>
        )}
        <Button variant="ghost" size="sm" className="ml-auto" onClick={dissolve}
          disabled={!merge || selecting}>
          <Unlink className="h-4 w-4" /> {t("merge.dissolve", "Undo merge")}
        </Button>
      </div>

      <section className="flex min-h-0 flex-col lg:flex-1 lg:flex-row">
        <div className="grid h-[42vh] min-h-0 grid-cols-2 gap-px bg-border lg:h-auto lg:flex-1">
          {merge && activeLayer ? (
            <SelectEditor src={activeLayer.photo.preview_url}
              label={activeLayer.photo.id === merge.base.id
                ? `${t("merge.base", "Base")} · ${activeLayer.photo.file_name}`
                : activeLayer.photo.file_name}
              photo={activeLayer.photo}
              mask={selecting ? selection.mask : activeLayer.mask}
              brush={selecting ? brush : undefined} busy={selecting && segmenting}
              onStroke={selecting ? addStroke : undefined} faint={!selecting} />
          ) : (
            <ImagePane label={merge ? `${t("merge.base", "Base")} · ${merge.base.file_name}` : ""}
              src={merge?.base.preview_url} alt={merge?.base.file_name} />
          )}
          <CompositePane label={t("merge.result", "Result")} base={merge?.base} pieces={pieces} />
        </div>

        <aside className="w-full border-t border-border bg-card lg:w-80 lg:shrink-0 lg:overflow-y-auto lg:border-l lg:border-t-0 scrollbar-thin">
          {selecting ? (
            <SelectionPanel editing selection={selection} brush={brush} onBrush={setBrush}
              busy={segmenting} smartAvailable={smartAvailable}
              onApply={applySelection} onCancel={exitSelect}
              onClear={() => setSelection({ mask: null, coverage: 0 })} />
          ) : merge && (
            <MergePanel merge={merge} activeId={active} onActivate={setActive}
              onSelectArea={startSelect} onPatch={patchLayers} onRamp={setRamp}
              onRemove={removeLayer} />
          )}
        </aside>
      </section>
    </div>
  );
}

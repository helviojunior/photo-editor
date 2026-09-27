import { useCallback, useRef, useState } from "react";
import api from "lib/api";
import { useI18n } from "i18n";
import { useDialog } from "contexts/DialogContext";
import { BRUSH_MAX, BRUSH_MIN, BRUSH_STEP } from "components/editor/LayersPanel";

const EMPTY = { mask: null, coverage: 0 };

/**
 * Seleção por pincel (o modo seleção do editor e as áreas do merge): a
 * máscara em construção, o pincel e os pedidos ao `/segment/` da foto.
 *
 * Os traços vão em FILA — cada um parte da máscara que o anterior devolveu —
 * e o token descarta respostas de uma seleção que já acabou (`reset`).
 * Sem `stroke`, o pedido só prepara o modelo para a foto (o primeiro traço
 * não paga o encoder) e devolve a área da máscara de partida.
 */
export default function useBrushSelection(initialSize = 0.04) {
  const { t } = useI18n();
  const { alert } = useDialog();
  const [selection, setSelectionState] = useState(EMPTY);
  const selectionRef = useRef(EMPTY);
  const setSelection = useCallback((sel) => { selectionRef.current = sel; setSelectionState(sel); }, []);
  const [brush, setBrushState] = useState({ size: initialSize, mode: "add", smart: true });
  const setBrush = useCallback((patch) => setBrushState((b) => ({ ...b, ...patch })), []);
  // [ e ]: um passo menor ou maior, dentro dos limites do slider.
  const stepBrush = useCallback((dir) => setBrushState((b) => ({ ...b, size: Math.round(
    Math.min(Math.max(b.size + dir * BRUSH_STEP, BRUSH_MIN), BRUSH_MAX) * 1000) / 1000 })), []);
  const [pending, setPending] = useState(0);
  const queue = useRef(Promise.resolve());
  const token = useRef(0);

  const request = useCallback((photoId, body) => {
    const mine = token.current;
    setPending((n) => n + 1);
    queue.current = queue.current.then(async () => {
      if (mine !== token.current) return;
      try {
        const res = await api.post(`/api/photos/${photoId}/segment/`,
          { ...body, base: selectionRef.current.mask });
        if (mine === token.current) {
          setSelection({ mask: res.data.mask, coverage: res.data.coverage });
        }
      } catch (err) {
        if (mine === token.current && body.stroke) {
          await alert({
            title: t("layers.segmentError", "Could not select the area"),
            description: err?.response?.data?.error || t("error.generic"),
            variant: "danger",
          });
        }
      } finally {
        setPending((n) => n - 1);
      }
    });
  }, [alert, t, setSelection]);

  // Começa (ou termina) uma seleção: descarta o que ainda está na fila.
  const reset = useCallback((mask = null) => {
    token.current += 1;
    setSelection({ mask, coverage: 0 });
  }, [setSelection]);

  return { selection, selectionRef, setSelection, brush, setBrush, stepBrush,
    busy: pending > 0, request, reset };
}

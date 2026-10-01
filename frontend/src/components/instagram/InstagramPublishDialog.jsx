import React, { useCallback, useEffect, useMemo, useState } from "react";
import { useNavigate } from "react-router-dom";
import { CheckCircle2, ExternalLink, Info, Instagram, RotateCcw } from "lucide-react";
import api from "lib/api";
import { cn } from "lib/utils";
import { useI18n } from "i18n";
import { Modal } from "components/ui/modal";
import { Button } from "components/ui/button";
import { FormErrors } from "components/ui/form-error";
import { SelectionCheck } from "components/ui/selection-check";
import { fillCaption } from "./caption";

export const DEFAULT_CAPTION_TEMPLATE = "{event}\n{date}\n\n{hashtag}";

/**
 * "Publicar no Instagram": escolhe as versões Instagram (até o limite do
 * carrossel), edita a legenda — já preenchida com os dados do evento — e
 * publica. Quem envia é o backend (services/instagram_publish.py), que antes
 * exporta cada versão para publicar/instagram/: sai exatamente o arquivo da
 * pasta.
 *
 * Fechar durante o envio não o cancela: ele segue no backend e, ao reabrir, o
 * modal volta a mostrar o andamento.
 */
export default function InstagramPublishDialog({ open, onClose, versions }) {
  const { t, tf, lang } = useI18n();
  const navigate = useNavigate();
  const [info, setInfo] = useState(null);
  const [account, setAccount] = useState(null);
  const [loadError, setLoadError] = useState(false);
  const [selected, setSelected] = useState(null);
  const [caption, setCaption] = useState(null);
  // O envio que este modal acompanha (o de antes de abrir só se ainda roda).
  const [job, setJob] = useState(null);
  const [error, setError] = useState("");
  const [starting, setStarting] = useState(false);

  const max = info?.max_photos || 10;
  const captionMax = info?.caption_max || 2200;
  const template = account?.caption_template
    || t("instagram.captionTemplate", DEFAULT_CAPTION_TEMPLATE);
  const refill = useCallback(
    () => setCaption(fillCaption(template, info?.event, lang)), [template, info, lang]);

  useEffect(() => {
    if (!open) return;
    setLoadError(false);
    setError("");
    Promise.all([api.get("/api/instagram/publish/"), api.get("/api/instagram/account/")])
      .then(([pub, acc]) => {
        setInfo(pub.data);
        setAccount(acc.data);
        setJob(pub.data.job?.running ? pub.data.job : null);
      })
      .catch(() => setLoadError(true));
  }, [open]);

  // Primeira abertura: legenda do modelo e as primeiras versões marcadas.
  useEffect(() => {
    if (info && account && caption === null) refill();
  }, [info, account, caption, refill]);
  useEffect(() => {
    if (open && info && selected === null) setSelected(versions.slice(0, max).map((p) => p.id));
  }, [open, info, versions, max, selected]);
  // Versão que saiu da filmstrip (excluída) sai da seleção.
  const picked = useMemo(
    () => versions.filter((p) => (selected || []).includes(p.id)), [versions, selected]);

  const running = !!job?.running;
  useEffect(() => {
    if (!running) return undefined;
    const timer = setInterval(() => {
      api.get("/api/instagram/publish/").then((res) => setJob(res.data.job)).catch(() => {});
    }, 1000);
    return () => clearInterval(timer);
  }, [running]);

  const toggle = (id) => {
    if (running) return;
    setSelected((list) => {
      const cur = list || [];
      if (cur.includes(id)) return cur.filter((x) => x !== id);
      return cur.length >= max ? cur : [...cur, id];
    });
  };

  const publish = async () => {
    setError("");
    setStarting(true);
    try {
      const res = await api.post("/api/instagram/publish/", {
        photos: picked.map((p) => p.id),
        caption: caption || "",
      });
      setJob(res.data.job);
    } catch (err) {
      setError(err?.response?.data?.error || t("error.generic"));
    } finally {
      setStarting(false);
    }
  };

  const goToSettings = () => {
    onClose();
    navigate("/settings/instagram");
  };

  const done = job && !job.running;
  const post = done ? job.post : null;
  const failed = done && job.error;
  // O Instagram corta o carrossel inteiro na proporção da PRIMEIRA foto.
  const mixedRatios = new Set(picked.map((p) => p.adjustments?.crop?.ratio)).size > 1;
  const tooLong = (caption || "").length > captionMax;

  const progress = !running ? ""
    : job.phase === "upload"
      ? t("instagram.publish.uploading", "Sending to Instagram…")
      : tf("instagram.publish.preparing", { done: job.done, total: job.total });

  let body;
  if (loadError) {
    body = <FormErrors items={[t("common.loadError")]} />;
  } else if (!info || !account) {
    body = <p className="text-muted-foreground">{t("common.loading")}</p>;
  } else if (post) {
    body = (
      <div className="flex flex-col items-center gap-3 py-2 text-center">
        <CheckCircle2 className="h-10 w-10 text-emerald-600 dark:text-emerald-400" aria-hidden="true" />
        <p className="font-medium">{t("instagram.publish.done", "Published on Instagram.")}</p>
        {post.url && (
          <a href={post.url} target="_blank" rel="noreferrer"
            className="inline-flex items-center gap-1 text-brand-400 hover:underline">
            <ExternalLink className="h-4 w-4" aria-hidden="true" />
            {t("instagram.publish.open", "Open the post")}
          </a>
        )}
      </div>
    );
  } else if (!account.connected && !running) {
    body = (
      <div className="space-y-3">
        <p className="flex items-start gap-2 rounded-md border border-border bg-muted/40 px-3 py-2 text-xs">
          <Info className="mt-px h-3.5 w-3.5 flex-shrink-0" aria-hidden="true" />
          {t("instagram.publish.noAccount",
            "No Instagram account is connected to the app yet. Connect one to publish.")}
        </p>
      </div>
    );
  } else {
    body = (
      <div className="space-y-4">
        {account.username && (
          <p className="text-xs text-muted-foreground">
            {tf("instagram.publish.as", { username: account.username })}{" "}
            <button type="button" onClick={goToSettings} className="text-brand-400 hover:underline">
              {t("instagram.publish.change", "Change")}
            </button>
          </p>
        )}

        <section>
          <div className="mb-1.5 flex items-baseline justify-between text-xs">
            <span className="font-medium">{t("instagram.publish.photos", "Photos")}</span>
            <span className="tabular-nums text-muted-foreground">
              {tf("instagram.publish.count", { count: picked.length, max })}
            </span>
          </div>
          {versions.length === 0 ? (
            <p className="text-xs text-muted-foreground">
              {t("instagram.publish.none", "No photo has an Instagram version. Press I on a photo to create one.")}
            </p>
          ) : (
            <div className="grid grid-cols-3 gap-2 sm:grid-cols-5">
              {versions.map((p) => {
                const checked = picked.some((x) => x.id === p.id);
                const order = picked.findIndex((x) => x.id === p.id);
                const full = !checked && picked.length >= max;
                return (
                  <div key={p.id} role="checkbox" aria-checked={checked} tabIndex={0}
                    aria-disabled={full || running || undefined}
                    title={p.file_name}
                    onClick={() => toggle(p.id)}
                    onKeyDown={(e) => {
                      if (e.key === " " || e.key === "Enter") { e.preventDefault(); toggle(p.id); }
                    }}
                    className={cn(
                      "relative aspect-square cursor-pointer overflow-hidden rounded-md bg-neutral-800 transition",
                      checked ? "ring-2 ring-brand-400" : "opacity-60 hover:opacity-100",
                      (full || running) && "cursor-not-allowed"
                    )}>
                    <img src={p.edited_url} alt={p.file_name} loading="lazy" draggable={false}
                      className="h-full w-full object-contain" />
                    <SelectionCheck checked={checked}
                      className="absolute left-1.5 top-1.5 bg-black/50 text-white" />
                    {checked && (
                      <span className="absolute bottom-1 right-1 rounded bg-black/70 px-1.5 text-[10px] font-semibold tabular-nums text-white">
                        {order + 1}
                      </span>
                    )}
                  </div>
                );
              })}
            </div>
          )}
          {mixedRatios && (
            <p className="mt-2 flex items-start gap-1.5 text-[11px] text-muted-foreground">
              <Info className="mt-px h-3 w-3 flex-shrink-0" aria-hidden="true" />
              {t("instagram.publish.mixed",
                "The photos have different formats: Instagram crops the whole carousel to the format of the first one.")}
            </p>
          )}
        </section>

        <section>
          <div className="mb-1.5 flex items-baseline justify-between text-xs">
            <label htmlFor="instagram-caption" className="font-medium">
              {t("instagram.publish.caption", "Caption")}
            </label>
            <button type="button" onClick={refill} disabled={running}
              className="inline-flex items-center gap-1 text-muted-foreground hover:text-foreground disabled:opacity-50">
              <RotateCcw className="h-3 w-3" aria-hidden="true" />
              {t("instagram.publish.refill", "Fill from the event")}
            </button>
          </div>
          <textarea id="instagram-caption" value={caption || ""} rows={7} disabled={running}
            onChange={(e) => setCaption(e.target.value)}
            className="w-full resize-y rounded-md border border-input bg-background px-3 py-2 text-sm text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-60" />
          <div className={cn("mt-0.5 text-right text-[11px] tabular-nums",
            tooLong ? "text-foreground" : "text-muted-foreground")}>
            {(caption || "").length}/{captionMax}
          </div>
          {tooLong && (
            <FormErrors items={[tf("instagram.publish.tooLong", { max: captionMax })]} />
          )}
        </section>

        {running && (
          <div>
            <div className="mb-1 text-xs text-muted-foreground" role="status">{progress}</div>
            <div className="h-2 w-full overflow-hidden rounded-full bg-muted">
              <div className="h-full rounded-full bg-brand-500 transition-[width] duration-300"
                style={{ width: `${job.phase === "upload" ? 100 : Math.round((job.done / (job.total || 1)) * 90)}%` }} />
            </div>
          </div>
        )}

        <FormErrors items={[
          error,
          failed && job.message,
          failed && job.detail,
        ]} />
      </div>
    );
  }

  const footer = post ? (
    <Button onClick={onClose}>{t("common.close")}</Button>
  ) : info && account && !account.connected && !running ? (
    <>
      <Button variant="outline" onClick={onClose}>{t("common.cancel")}</Button>
      <Button onClick={goToSettings}>
        <Instagram className="h-4 w-4" /> {t("instagram.publish.connect", "Connect account")}
      </Button>
    </>
  ) : (
    <>
      <Button variant="outline" onClick={onClose}>
        {running ? t("export.background", "Keep running in background") : t("common.cancel")}
      </Button>
      <Button onClick={publish} loading={running || starting}
        disabled={!info || !picked.length || tooLong || running}>
        {!(running || starting) && <Instagram className="h-4 w-4" />}
        {t("instagram.publish.button", "Publish")}
      </Button>
    </>
  );

  return (
    <Modal open={open} onClose={onClose} size="xl"
      className="max-h-[calc(100dvh-2rem)] overflow-y-auto"
      title={t("instagram.publish.title", "Publish on Instagram")}
      icon={
        <div className="flex h-12 w-12 items-center justify-center rounded-full bg-brand-400/15">
          <Instagram className="h-5 w-5 text-brand-400" />
        </div>
      }
      footer={footer}>
      <div className="text-left">{body}</div>
    </Modal>
  );
}

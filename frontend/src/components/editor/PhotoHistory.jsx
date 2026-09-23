import React, { useEffect, useState } from "react";
import api from "lib/api";
import { useI18n } from "i18n";
import { cn } from "lib/utils";

/**
 * Historico completo da foto (TODO 5.7), mais recente primeiro. O que ja foi
 * desfeito continua na lista, riscado: o banco guarda tudo.
 */
export default function PhotoHistory({ photoId, version }) {
  const { t } = useI18n();
  const [entries, setEntries] = useState([]);

  useEffect(() => {
    if (!photoId) return undefined;
    let alive = true;
    api.get(`/api/photos/${photoId}/history/`)
      .then((res) => { if (alive) setEntries(res.data.results); })
      .catch(() => { if (alive) setEntries([]); });
    return () => { alive = false; };
  }, [photoId, version]);

  return (
    <section className="p-4">
      <h2 className="mb-2 text-[11px] font-semibold uppercase tracking-wide text-muted-foreground">
        {t("history.title", "History")}
      </h2>
      {entries.length === 0 ? (
        <p className="text-xs text-muted-foreground">{t("history.empty", "No actions yet.")}</p>
      ) : (
        <ol className="space-y-1 text-xs">
          {entries.map((e) => (
            <li key={e.id} className="flex items-baseline justify-between gap-2">
              <span className={cn(e.undone_at && "text-muted-foreground line-through")}>
                {t(`action.${e.kind}`, e.kind)}
              </span>
              <time className="shrink-0 text-[11px] text-muted-foreground" dateTime={e.created}>
                {new Date(e.created).toLocaleString()}
              </time>
            </li>
          ))}
        </ol>
      )}
    </section>
  );
}

import React from "react";
import { CheckCircle2, Upload } from "lucide-react";
import { useI18n } from "i18n";
import { Modal } from "components/ui/modal";
import { Button } from "components/ui/button";
import { FormErrors } from "components/ui/form-error";

/**
 * Progresso e resumo da exportação (TODO 7.3), no modal do sistema (regra 1).
 *
 * Fechar durante a exportação não a cancela: ela segue no backend, e o botão
 * Exportar mostra o progresso até reabrir.
 */
export default function ExportDialog({ open, status, onClose }) {
  const { t, tf } = useI18n();
  if (!status) return null;

  const running = status.running;
  const pct = status.total ? Math.round((status.done / status.total) * 100) : 0;
  const errors = status.errors.filter((e) => e !== "*");

  return (
    <Modal
      open={open}
      onClose={onClose}
      size="md"
      title={running ? t("export.running", "Exporting…") : t("export.done", "Export finished")}
      icon={
        <div className={running
          ? "flex h-12 w-12 items-center justify-center rounded-full bg-brand-400/15"
          : "flex h-12 w-12 items-center justify-center rounded-full bg-emerald-100 dark:bg-emerald-900/30"}>
          {running
            ? <Upload className="h-5 w-5 text-brand-400" />
            : <CheckCircle2 className="h-5 w-5 text-emerald-600 dark:text-emerald-400" />}
        </div>
      }
      description={tf("export.destination", { dir: `${status.output_dir}/` })}
      footer={
        <Button variant={running ? "outline" : "default"} onClick={onClose}>
          {running ? t("export.background", "Keep running in background") : t("common.close")}
        </Button>
      }
    >
      <div className="space-y-3 text-left">
        <div>
          <div className="mb-1 flex justify-between text-xs text-muted-foreground">
            <span>{tf("export.progress", { done: status.done, total: status.total })}</span>
            <span className="tabular-nums">{pct}%</span>
          </div>
          <div className="h-2 w-full overflow-hidden rounded-full bg-muted"
            role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={pct}>
            <div className="h-full rounded-full bg-brand-500 transition-[width] duration-300"
              style={{ width: `${pct}%` }} />
          </div>
        </div>

        {!running && (
          <dl className="grid grid-cols-3 gap-2 text-center">
            {[
              ["written", status.written],
              ["skipped", status.skipped],
              ["removed", status.removed],
            ].map(([key, n]) => (
              <div key={key} className="rounded-lg border border-border p-2">
                <dd className="text-lg font-semibold tabular-nums">{n}</dd>
                <dt className="text-[11px] text-muted-foreground">{t(`export.${key}`, key)}</dt>
              </div>
            ))}
          </dl>
        )}

        {status.errors.length > 0 && (
          <FormErrors items={[
            t("export.errors", "Some photos could not be exported:"),
            ...(errors.length ? errors : [t("error.generic")]),
          ]} />
        )}
      </div>
    </Modal>
  );
}

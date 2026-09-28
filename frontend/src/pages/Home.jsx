import React, { useCallback, useEffect, useMemo, useState } from "react";
import {
  FolderOpen, FolderPlus, FolderSearch, FolderX, ImageOff, Search, X,
} from "lucide-react";
import api from "lib/api";
import { cn } from "lib/utils";
import { desktopAction, formatBytes } from "lib/desktop";
import { useI18n } from "i18n";
import { useDialog } from "contexts/DialogContext";
import { FormError } from "components/ui/form-error";

/**
 * Tela inicial do app desktop: abrir/criar projeto e os projetos recentes.
 *
 * Um "projeto" é a pasta de um evento (raw/ + project_data/ ...). Quem abre de
 * fato é o shell desktop — escolher pasta e reiniciar o servidor local são
 * coisas do SO (lib/desktop.js). A lista vem do banco do app
 * (~/.photoe/photoe.db) via /api/projects/recent/.
 */
export default function Home() {
  const { t, tf, lang } = useI18n();
  const { confirm } = useDialog();
  const [projects, setProjects] = useState(null);
  const [current, setCurrent] = useState(null);
  const [desktop, setDesktop] = useState(true);
  const [error, setError] = useState(false);
  const [query, setQuery] = useState("");

  const load = useCallback(() => {
    return api.get("/api/projects/recent/")
      .then((res) => {
        setProjects(res.data.projects || []);
        setCurrent(res.data.current);
        setError(false);
      })
      .catch(() => setError(true));
  }, []);

  useEffect(() => {
    load();
    api.get("/api/config/").then((res) => setDesktop(!!res.data?.desktop)).catch(() => {});
  }, [load]);

  const visible = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!projects || !q) return projects || [];
    return projects.filter((p) => p.name.toLowerCase().includes(q)
      || p.path.toLowerCase().includes(q));
  }, [projects, query]);

  const remove = (project) => confirm({
    title: t("home.removeTitle", "Remove from the list?"),
    description: <>
      <strong>{project.name}</strong> {t("home.removeText",
        "leaves the recent projects. The folder and its photos are not touched.")}
    </>,
    variant: "warning",
    confirmLabel: t("home.remove", "Remove from list"),
    onConfirm: async () => {
      await api.delete("/api/projects/recent/", { data: { path: project.path } });
      await load();
    },
  });

  const open = (project) => {
    if (!project.exists) {
      remove(project);
      return;
    }
    if (desktop) desktopAction("open", { path: project.path });
  };

  return (
    <div className="w-full space-y-8">
      <div className="grid gap-4 md:grid-cols-3">
        <button
          type="button"
          disabled={!desktop}
          onClick={() => desktopAction("open-dialog")}
          className={cn(
            "group relative overflow-hidden rounded-2xl md:col-span-2 min-h-[132px] lg:min-h-[168px]",
            "flex items-center justify-center gap-3 text-white text-lg font-semibold",
            "bg-gradient-to-br from-brand-600 via-brand-800 to-zinc-950",
            "ring-1 ring-white/10 transition focus-visible:outline-none",
            "focus-visible:ring-2 focus-visible:ring-brand-300 disabled:opacity-50",
          )}
        >
          <span aria-hidden className="pointer-events-none absolute inset-0 opacity-70
            bg-[radial-gradient(ellipse_at_30%_120%,rgba(248,94,98,.55),transparent_60%),radial-gradient(ellipse_at_85%_-20%,rgba(255,255,255,.18),transparent_55%)]
            transition-opacity group-hover:opacity-100" />
          <span className="relative flex h-10 w-10 items-center justify-center rounded-lg bg-white/15">
            <FolderOpen className="h-5 w-5" />
          </span>
          <span className="relative text-left">
            <span className="block">{t("home.open", "Open project")}</span>
            <span className="block text-xs font-normal text-white/70">
              {t("home.openHint", "Choose the event folder (the one with raw/ inside)")}
            </span>
          </span>
        </button>

        <button
          type="button"
          disabled={!desktop}
          onClick={() => desktopAction("new")}
          className="touch-target flex items-center gap-3 rounded-2xl border border-border bg-card px-5 py-4
            text-left transition-colors hover:bg-muted focus-visible:outline-none focus-visible:ring-2
            focus-visible:ring-brand-400 disabled:opacity-50"
        >
          <span className="flex h-10 w-10 flex-none items-center justify-center rounded-lg bg-brand-500/15">
            <FolderPlus className="h-5 w-5 text-brand-400" />
          </span>
          <span>
            <span className="block text-sm font-semibold">{t("home.new", "New project")}</span>
            <span className="block text-xs text-muted-foreground">
              {t("home.newHint", "Pick a folder and add the event photos")}
            </span>
          </span>
        </button>
      </div>

      {!desktop && (
        <FormError>{t("home.desktopOnly", "Opening projects needs the PhotoEditor desktop app.")}</FormError>
      )}

      <section className="space-y-4">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <h2 className="text-sm font-semibold">
            {t("home.recent", "Projects")}
            {projects && <span className="ml-1 font-normal text-muted-foreground">({projects.length})</span>}
          </h2>
          {projects && projects.length > 0 && (
            <label className="relative flex items-center">
              <Search className="pointer-events-none absolute left-2.5 h-4 w-4 text-muted-foreground" />
              <input
                type="search"
                value={query}
                onChange={(e) => setQuery(e.target.value)}
                placeholder={t("home.search", "Search projects")}
                aria-label={t("home.search", "Search projects")}
                className="touch-target h-9 w-56 rounded-md border border-input bg-transparent pl-8 pr-3 text-sm
                  focus:outline-none focus:ring-2 focus:ring-brand-400"
              />
            </label>
          )}
        </div>

        {error && <FormError>{t("home.loadError", "Could not load the recent projects.")}</FormError>}

        {projects && projects.length === 0 && (
          <div className="rounded-2xl border border-dashed border-border px-6 py-12 text-center">
            <p className="text-sm font-medium">{t("home.empty", "No projects yet")}</p>
            <p className="mt-1 text-xs text-muted-foreground">
              {t("home.emptyHint", "Open an event folder to start — it will be listed here next time.")}
            </p>
          </div>
        )}

        <div className="grid grid-cols-[repeat(auto-fill,minmax(150px,1fr))] gap-x-4 gap-y-6">
          {visible.map((project) => (
            <ProjectCard
              key={project.path}
              project={project}
              isCurrent={project.path === current}
              lang={lang}
              t={t}
              tf={tf}
              onOpen={() => open(project)}
              onRemove={() => remove(project)}
              onReveal={desktop && project.exists
                ? () => desktopAction("reveal", { path: project.path })
                : null}
            />
          ))}
        </div>
      </section>
    </div>
  );
}

function ProjectCard({ project, isCurrent, lang, t, tf, onOpen, onRemove, onReveal }) {
  const [coverFailed, setCoverFailed] = useState(false);
  const cover = `/api/projects/cover/?${new URLSearchParams({
    path: project.path, v: `${project.photo_count}-${project.size_bytes}`,
  })}`;
  const openedAt = project.opened_at
    ? new Date(project.opened_at).toLocaleDateString(lang, { day: "2-digit", month: "short", year: "numeric" })
    : "";

  let placeholder = null;
  if (!project.exists) placeholder = <FolderX className="h-8 w-8" />;
  else if (!project.photo_count || coverFailed) placeholder = <ImageOff className="h-8 w-8" />;

  return (
    <div
      role="button"
      tabIndex={0}
      title={project.path}
      onClick={onOpen}
      onKeyDown={(e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          onOpen();
        }
      }}
      className="group cursor-pointer rounded-xl focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-brand-400"
    >
      <div className={cn(
        "relative aspect-square overflow-hidden rounded-xl bg-muted ring-1 ring-border transition",
        "group-hover:ring-2 group-hover:ring-brand-400",
        !project.exists && "opacity-60",
      )}>
        {placeholder ? (
          <div className="flex h-full w-full flex-col items-center justify-center gap-2 text-muted-foreground">
            {placeholder}
            {!project.exists && <span className="text-[11px]">{t("home.missing", "Folder not found")}</span>}
          </div>
        ) : (
          <img
            src={cover}
            alt=""
            loading="lazy"
            draggable={false}
            onError={() => setCoverFailed(true)}
            className="h-full w-full object-cover transition-transform duration-300 group-hover:scale-[1.03]"
          />
        )}

        {isCurrent && (
          <span className="absolute left-2 top-2 rounded-full bg-brand-500 px-2 py-0.5 text-[10px] font-semibold text-white">
            {t("home.current", "Open")}
          </span>
        )}

        <div className="absolute right-1.5 top-1.5 flex gap-1 opacity-0 transition-opacity
          group-hover:opacity-100 group-focus-within:opacity-100 [@media(pointer:coarse)]:opacity-100">
          {onReveal && (
            <CardIconButton label={t("home.reveal", "Show in folder")} onClick={onReveal}>
              <FolderSearch className="h-3.5 w-3.5" />
            </CardIconButton>
          )}
          <CardIconButton label={t("home.remove", "Remove from list")} onClick={onRemove}>
            <X className="h-3.5 w-3.5" />
          </CardIconButton>
        </div>
      </div>

      <div className="mt-2 px-0.5">
        <p className="truncate text-sm font-medium">{project.name}</p>
        <p className="truncate text-xs text-muted-foreground">
          {project.exists
            ? `${tf("home.photos", { count: project.photo_count })} · ${formatBytes(project.size_bytes, lang)}`
            : project.path}
        </p>
        {openedAt && (
          <p className="truncate text-[11px] text-muted-foreground/80">
            {tf("home.opened", { date: openedAt })}
          </p>
        )}
      </div>
    </div>
  );
}

function CardIconButton({ label, onClick, children }) {
  return (
    <button
      type="button"
      aria-label={label}
      title={label}
      onClick={(e) => {
        e.stopPropagation();
        onClick();
      }}
      onKeyDown={(e) => e.stopPropagation()}
      className="touch-target flex h-7 w-7 items-center justify-center rounded-full bg-black/60 text-white
        backdrop-blur hover:bg-black/80"
    >
      {children}
    </button>
  );
}

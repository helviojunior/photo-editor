import React, { useEffect, useState } from "react";
import { Outlet, useLocation, useNavigate } from "react-router-dom";
import { House, Instagram, Moon, Sun } from "lucide-react";
import api from "lib/api";
import { cn } from "lib/utils";
import brand from "lib/brand";
import { desktopAction } from "lib/desktop";
import { useI18n, LANGUAGE_OPTIONS } from "i18n";

/**
 * Casca da aplicação: cabeçalho + conteúdo, sem barra lateral.
 *
 * O editor é a única tela e precisa de toda a largura para o antes/depois e o
 * painel de edição (TODO 3.4). Um menu com um item só roubava 208px para não
 * levar a lugar nenhum. Se surgirem outras telas, o `SidebarShell` de
 * `./Sidebar` continua disponível para voltar com a navegação.
 */
export default function AppLayout({ darkMode, setDarkMode }) {
  const { t, lang, setLanguage, adoptSystemDefault } = useI18n();
  const location = useLocation();
  const navigate = useNavigate();
  const fullBleed = /^\/(photos|merges)(\/|$)/.test(location.pathname);
  // Projeto aberto e se ha app desktop em volta (para o botao Inicio).
  const [config, setConfig] = useState(null);

  // Padrao de idioma do sistema: so vale se nem cookie nem navegador responderam.
  useEffect(() => {
    api.get("/api/config/")
      .then((res) => {
        setConfig(res.data);
        adoptSystemDefault(res.data?.default_language);
      })
      .catch(() => {});
  }, [adoptSystemDefault]);

  // Nome do projeto no titulo da pagina (o shell tambem o poe na janela).
  useEffect(() => {
    const name = config?.project?.name;
    document.title = name ? `${name} — ${brand.name}` : brand.name;
  }, [config]);

  const project = config?.project;

  return (
    <div className="flex flex-col h-dvh overflow-hidden bg-background">
      {/* Cabeçalho baixo: cada pixel de altura que ele não usa vai para as fotos. */}
      <header className="flex items-center justify-between gap-2 bg-card border-b border-border px-2 lg:px-4 z-50">
        <div className="flex items-center gap-2">
          <img
            src={darkMode ? brand.logoDark : brand.logo}
            alt={brand.name}
            className="h-6 w-auto drop-shadow-[0_0_10px_rgba(255,255,255,0.4)]"
          />
          <span className="hidden sm:inline text-[11px] text-muted-foreground">
            v{brand.version}
          </span>
          {/* Voltar a tela inicial troca de projeto: quem faz e o shell desktop. */}
          {config?.desktop && project && (
            <>
              <div className="h-6 w-px bg-border mx-1" />
              <button
                onClick={() => desktopAction("home")}
                aria-label={t("nav.home", "Home")}
                title={t("nav.home", "Home")}
                className="touch-target inline-flex h-10 w-10 items-center justify-center rounded-full text-muted-foreground hover:text-foreground hover:bg-muted transition-colors"
              >
                <House size={18} />
              </button>
              <span className="truncate max-w-[40vw] text-sm font-medium" title={project.path}>
                {project.name}
              </span>
            </>
          )}
        </div>

        <div className="flex items-center gap-1 lg:gap-3">
          {/* Idioma da sessao — cookie -> navegador -> padrao do sistema */}
          <select
            value={lang}
            onChange={(e) => setLanguage(e.target.value)}
            aria-label={t("common.language")}
            className="h-10 touch-target bg-transparent text-sm text-muted-foreground hover:text-foreground focus:outline-none cursor-pointer"
          >
            {LANGUAGE_OPTIONS.map((l) => (
              <option key={l.value} value={l.value} className="bg-card text-foreground">
                {l.label}
              </option>
            ))}
          </select>

          <div className="hidden lg:block h-6 w-px bg-border" />

          {/* Conta do Instagram para o "Publicar" (vale para todos os projetos). */}
          <button
            onClick={() => navigate("/settings/instagram")}
            aria-label={t("instagram.settings.open", "Instagram settings")}
            title={t("instagram.settings.open", "Instagram settings")}
            aria-current={location.pathname === "/settings/instagram" ? "page" : undefined}
            className={cn(
              "touch-target inline-flex h-10 w-10 items-center justify-center rounded-full hover:bg-muted transition-colors",
              location.pathname === "/settings/instagram"
                ? "text-brand-400" : "text-muted-foreground hover:text-foreground"
            )}
          >
            <Instagram size={18} />
          </button>

          <button
            onClick={() => setDarkMode(!darkMode)}
            aria-label={t("nav.theme", "Theme")}
            className="touch-target inline-flex h-10 w-10 items-center justify-center rounded-full text-muted-foreground hover:text-foreground hover:bg-muted transition-colors"
          >
            {darkMode ? <Sun size={18} /> : <Moon size={18} />}
          </button>
        </div>
      </header>

      {/* O editor ocupa a area inteira, sem margem: e' ele quem divide a
          altura em 70/30. As demais telas mantem o respiro padrao. */}
      <main className={cn(
        "flex-1 overflow-y-auto bg-background relative",
        fullBleed ? "p-0 lg:overflow-hidden" : "p-4 lg:p-6"
      )}>
        <Outlet />
      </main>
    </div>
  );
}

import React, { useEffect } from "react";
import { Outlet, useLocation } from "react-router-dom";
import { Moon, Sun } from "lucide-react";
import api from "lib/api";
import { cn } from "lib/utils";
import brand from "lib/brand";
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
  const fullBleed = location.pathname.startsWith("/photos");

  // Padrao de idioma do deploy: so vale se nem cookie nem navegador responderam.
  useEffect(() => {
    api.get("/api/config/")
      .then((res) => adoptSystemDefault(res.data?.default_language))
      .catch(() => {});
  }, [adoptSystemDefault]);

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

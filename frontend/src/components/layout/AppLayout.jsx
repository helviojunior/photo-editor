import React, { useState, useEffect, useRef } from "react";
import { Outlet, Link, useLocation } from "react-router-dom";
import { createPortal } from "react-dom";
import {
  LayoutDashboard,
  Moon,
  Sun,
  ChevronLeft,
  ChevronRight,
  ChevronDown,
} from "lucide-react";
import api from "lib/api";
import { cn } from "lib/utils";
import brand from "lib/brand";
import { useI18n, LANGUAGE_OPTIONS } from "i18n";
import { MenuButton, SidebarShell, useIsMobile } from "./Sidebar";

// Itens com `children` viram submenu (acordeao expandido / flyout colapsado).
const menuStructure = [
  {
    id: "dashboard",
    path: "/dashboard",
    icon: LayoutDashboard,
    labelKey: "nav.dashboard",
    children: null,
  },
];

const findActiveMenu = (pathname) => {
  for (const menu of menuStructure) {
    if (menu.path === pathname) {
      return { menuId: menu.id, subMenuId: null };
    }
    if (menu.children) {
      const sortedChildren = [...menu.children].sort(
        (a, b) => b.path.length - a.path.length
      );
      for (const child of sortedChildren) {
        if (pathname === child.path || pathname.startsWith(child.path + "/")) {
          return { menuId: menu.id, subMenuId: child.id };
        }
      }
    }
  }
  return { menuId: "dashboard", subMenuId: null };
};

export default function AppLayout({ darkMode, setDarkMode }) {
  const { t, lang, setLanguage, adoptSystemDefault } = useI18n();
  const [sidebarCollapsed, setSidebarCollapsed] = useState(() => {
    const saved = localStorage.getItem("sidebarCollapsed");
    return saved === "true";
  });
  const [expandedMenus, setExpandedMenus] = useState({});
  const [hoveredMenu, setHoveredMenu] = useState(null);
  const [flyoutPosition, setFlyoutPosition] = useState({ top: 0 });
  const [drawerOpen, setDrawerOpen] = useState(false);
  const isMobile = useIsMobile();
  const hoverTimeoutRef = useRef(null);
  const location = useLocation();

  const { menuId: activeMenuId, subMenuId: activeSubMenuId } =
    findActiveMenu(location.pathname);

  // Padrao de idioma do deploy: so vale se nem cookie nem navegador responderam.
  useEffect(() => {
    api.get("/api/config/")
      .then((res) => adoptSystemDefault(res.data?.default_language))
      .catch(() => {});
  }, [adoptSystemDefault]);

  useEffect(() => {
    if (!sidebarCollapsed && activeMenuId) {
      setExpandedMenus({ [activeMenuId]: true });
    }
  }, [activeMenuId, sidebarCollapsed]);

  // Navegou: a gaveta sai de cena, senao cobre a tela que o clique abriu.
  useEffect(() => { setDrawerOpen(false); }, [location.pathname]);

  useEffect(() => {
    localStorage.setItem("sidebarCollapsed", sidebarCollapsed);
  }, [sidebarCollapsed]);

  useEffect(() => {
    if (!sidebarCollapsed) setHoveredMenu(null);
  }, [sidebarCollapsed]);

  // Na gaveta o menu abre SEMPRE expandido: ali o espaco e' da navegacao, e um
  // trilho de icones dentro de uma gaveta de 288px seria esconder o rotulo sem
  // ganhar nada. Isso tambem tira o celular do caminho do flyout por hover —
  // em tela estreita o submenu passa a ser o acordeao, que responde a toque.
  const effectiveCollapsed = !isMobile && sidebarCollapsed;

  const toggleMenu = (menuId, event) => {
    if (!effectiveCollapsed) {
      setExpandedMenus((prev) => ({ ...prev, [menuId]: !prev[menuId] }));
      return;
    }
    // Colapsado, o submenu so abria no `onMouseEnter`. Num tablet ou num
    // notebook com tela sensivel ao toque nao existe hover, e o item virava um
    // botao que nao faz nada — sem erro e sem pista do motivo.
    setHoveredMenu((prev) => {
      if (prev === menuId) return null;
      if (event && event.currentTarget) {
        setFlyoutPosition({ top: event.currentTarget.getBoundingClientRect().top });
      }
      return menuId;
    });
  };

  const handleMenuHover = (menuId, event) => {
    if (effectiveCollapsed) {
      clearTimeout(hoverTimeoutRef.current);
      setHoveredMenu(menuId);
      if (event && event.currentTarget) {
        const rect = event.currentTarget.getBoundingClientRect();
        setFlyoutPosition({ top: rect.top });
      }
    }
  };

  const handleMenuLeave = () => {
    if (effectiveCollapsed) {
      hoverTimeoutRef.current = setTimeout(() => setHoveredMenu(null), 150);
    }
  };

  const renderMenuItem = (item) => {
    const Icon = item.icon;
    const hasChildren = item.children && item.children.length > 0;
    const isExpanded = expandedMenus[item.id];
    const isActive = item.id === activeMenuId;
    const isHovered = hoveredMenu === item.id;

    if (!hasChildren) {
      return (
        <div key={item.id}>
          <Link
            to={item.path}
            className={cn(
              "flex min-h-11 items-center transition-all text-sm relative",
              effectiveCollapsed
                ? "justify-center p-2.5"
                : "justify-start gap-3 px-4 py-2.5",
              isActive
                ? "text-brand-400 font-medium"
                : "text-muted-foreground hover:text-foreground hover:bg-muted/30"
            )}
          >
            <Icon size={18} />
            {!effectiveCollapsed && (
              <span className="font-medium text-sm">{t(item.labelKey)}</span>
            )}
          </Link>
        </div>
      );
    }

    return (
      <div
        key={item.id}
        className="relative"
        onMouseEnter={(e) => handleMenuHover(item.id, e)}
        onMouseLeave={handleMenuLeave}
      >
        <button
          onClick={(e) => toggleMenu(item.id, e)}
          className={cn(
            "w-full flex min-h-11 items-center transition-all text-sm",
            effectiveCollapsed
              ? "justify-center p-2.5"
              : "justify-between px-4 py-2.5",
            isActive
              ? "text-foreground"
              : "text-muted-foreground hover:text-foreground hover:bg-muted/30"
          )}
        >
          <div className="flex items-center gap-3">
            <Icon size={18} />
            {!effectiveCollapsed && (
              <span className="font-medium text-sm">{t(item.labelKey)}</span>
            )}
          </div>
          {!effectiveCollapsed && (
            <ChevronDown
              size={16}
              className={cn(
                "transition-transform duration-200",
                isExpanded ? "rotate-180" : ""
              )}
            />
          )}
        </button>

        {effectiveCollapsed &&
          isHovered &&
          createPortal(
            <div
              className="fixed min-w-[180px]"
              style={{
                left: "72px",
                top: `${flyoutPosition.top}px`,
                zIndex: 99999,
              }}
              onMouseEnter={(e) => handleMenuHover(item.id, e)}
              onMouseLeave={handleMenuLeave}
            >
              <div className="absolute left-0 top-3 -ml-1.5 w-3 h-3 bg-popover border-l border-t border-border rotate-[-45deg]" style={{ zIndex: 99999 }} />
              <div className="bg-popover border border-border rounded-lg shadow-2xl py-2 ml-1">
                <div className="px-3 py-2 border-b border-border">
                  <span className="font-semibold text-sm">{t(item.labelKey)}</span>
                </div>
                {item.children.map((child) => {
                  const isChildActive = child.id === activeSubMenuId;
                  return (
                    <Link
                      key={child.id}
                      to={child.path}
                      className={cn(
                        "flex min-h-11 items-center px-4 py-2 text-sm transition-colors",
                        isChildActive
                          ? "text-brand-400 bg-brand-400/10 font-medium"
                          : "text-muted-foreground hover:bg-muted hover:text-foreground"
                      )}
                      onClick={() => setHoveredMenu(null)}
                    >
                      {t(child.labelKey)}
                    </Link>
                  );
                })}
              </div>
            </div>,
            document.body
          )}

        {!effectiveCollapsed && isExpanded && (
          <div className="space-y-0.5 bg-black/20">
            {item.children.map((child) => {
              const isChildActive = child.id === activeSubMenuId;
              return (
                <Link
                  key={child.id}
                  to={child.path}
                  className={cn(
                    "flex min-h-11 w-full items-center pl-11 pr-4 py-2 text-sm transition-colors",
                    isChildActive
                      ? "text-brand-400 font-medium"
                      : "text-muted-foreground/70 hover:text-foreground hover:bg-white/5"
                  )}
                >
                  {t(child.labelKey)}
                </Link>
              );
            })}
          </div>
        )}
      </div>
    );
  };

  return (
    <div className="flex flex-col h-dvh overflow-hidden bg-background">
      {/* Top Header */}
      <header className="flex items-center justify-between bg-card border-b border-border z-50">
        {/* A marca so reserva a coluna da barra quando a barra existe; em tela
            estreita ela e' uma gaveta e nao ocupa largura nenhuma. */}
        <div className="flex items-center gap-2 px-2 py-3 lg:justify-center lg:gap-0 lg:px-4 lg:w-52 lg:border-r lg:border-border">
          <MenuButton onClick={() => setDrawerOpen(true)}
            label={t("nav.openMenu", "Open menu")} />
          <img
            src={darkMode ? brand.logoDark : brand.logo}
            alt={brand.name}
            className="h-7 w-auto drop-shadow-[0_0_10px_rgba(255,255,255,0.4)]"
          />
        </div>

        <div className="flex items-center gap-1 flex-1 justify-end px-2 py-3 lg:gap-4 lg:px-6">
          {/* Idioma da sessao — cookie -> navegador -> padrao do sistema */}
          <select
            value={lang}
            onChange={(e) => setLanguage(e.target.value)}
            aria-label={t("common.language")}
            className="h-11 bg-transparent text-sm text-muted-foreground hover:text-foreground focus:outline-none cursor-pointer"
          >
            {LANGUAGE_OPTIONS.map((l) => (
              <option key={l.value} value={l.value} className="bg-card text-foreground">
                {l.label}
              </option>
            ))}
          </select>

          <div className="hidden lg:block h-6 w-px bg-border" />

          {/* Dark Mode Toggle */}
          <button
            onClick={() => setDarkMode(!darkMode)}
            aria-label={t("nav.theme", "Theme")}
            className="inline-flex h-11 w-11 items-center justify-center rounded-full text-muted-foreground hover:text-foreground hover:bg-muted transition-colors"
          >
            {darkMode ? <Sun size={20} /> : <Moon size={20} />}
          </button>
        </div>
      </header>

      {/* Main container - Sidebar + Content */}
      <div className="flex flex-1 overflow-hidden">
        {/* Sidebar */}
        <SidebarShell isMobile={isMobile} open={drawerOpen}
          width={effectiveCollapsed ? "w-16" : "w-52"}
          onClose={() => setDrawerOpen(false)}
          closeLabel={t("nav.closeMenu", "Close menu")}>
          <div className="flex flex-col h-full">
            <nav
              className={cn(
                "flex-1 space-y-0.5 overflow-y-auto scrollbar-thin pt-2",
                effectiveCollapsed ? "px-2" : "px-0"
              )}
            >
              {menuStructure.map(renderMenuItem)}
            </nav>

            {/* Footer with collapse toggle */}
            <div
              className={cn(
                "border-t border-border transition-all duration-300 flex items-end relative",
                isMobile ? "hidden" : "flex",
                effectiveCollapsed
                  ? "p-2 justify-center min-h-[60px]"
                  : "p-3 justify-between min-h-[70px]"
              )}
            >
              {!effectiveCollapsed && (
                <div className="text-xs text-muted-foreground">
                  <div className="text-[11px] font-medium">
                    {brand.name} v{brand.version}
                  </div>
                  <div className="text-[10px] text-muted-foreground/60">
                    &copy; {brand.name} {new Date().getFullYear()}
                  </div>
                </div>
              )}

              <button
                onClick={() => setSidebarCollapsed(!sidebarCollapsed)}
                className={cn(
                  "flex items-center justify-center transition-all duration-200",
                  effectiveCollapsed
                    ? "w-10 h-10 rounded-full bg-primary/10 hover:bg-primary/20 text-primary border border-primary/30 hover:border-primary/50 shadow-sm"
                    : "absolute right-0 bottom-[10px] w-[35px] h-[45px] rounded-l-lg bg-muted/50 hover:bg-muted text-muted-foreground hover:text-foreground"
                )}
              >
                {effectiveCollapsed ? (
                  <ChevronRight size={18} />
                ) : (
                  <ChevronLeft size={20} />
                )}
              </button>
            </div>
          </div>
        </SidebarShell>

        {/* Page Content */}
        <main className="flex-1 overflow-y-auto p-4 lg:p-6 bg-background relative">
          <Outlet />
        </main>
      </div>
    </div>
  );
}

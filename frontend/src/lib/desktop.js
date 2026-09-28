/**
 * Ponte com o app desktop (desktop/browser.py).
 *
 * O que só o SO sabe fazer — escolher a pasta do projeto, abrir no
 * Finder/Explorer, trocar de projeto (que reinicia o servidor local) — é pedido
 * NAVEGANDO para `/__desktop__/<ação>`. O shell intercepta essa navegação antes
 * de ela sair da página; nada chega ao servidor. Fora do app desktop (ex.:
 * `yarn start` num navegador comum) a navegação cairia num 404 — por isso a
 * tela confere `config.desktop` antes de oferecer essas ações.
 *
 * O prefixo está repetido em desktop/browser.py (ACTION_PREFIX).
 */
export const DESKTOP_ACTION_PREFIX = "/__desktop__/";

export function desktopAction(action, params = {}) {
  const query = new URLSearchParams(params).toString();
  window.location.assign(`${DESKTOP_ACTION_PREFIX}${action}${query ? `?${query}` : ""}`);
}

/** "1,2 GB" — tamanho legível, no formato do idioma da tela. */
export function formatBytes(bytes, lang) {
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = bytes || 0;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  const digits = unit === 0 || value >= 100 ? 0 : 1;
  return `${value.toLocaleString(lang, { maximumFractionDigits: digits })} ${units[unit]}`;
}

/**
 * Resolução do idioma — o sistema é público, sem conta para guardar preferência.
 *
 *     cookie  ->  navegador  ->  padrão do sistema
 *
 * Fica FORA do `i18n/index.js` de propósito: aquele arquivo tem JSX (o
 * Provider) e não pode ser carregado por um teste em node. Isto aqui é decisão
 * pura, com três ramos que importam, e decisão pura merece ser testável sem
 * montar React em volta.
 */
export const SUPPORTED = ["en", "pt-br"];

/** Último recurso do CATÁLOGO (regra 4): chave sem tradução cai para EN.
 *  Não confundir com o padrão do sistema, que é configurável por deploy e
 *  chega pelo /api/config/. */
export const FALLBACK = "en";

/**
 * Cookie de longa duração com o último idioma usado.
 *
 * Quem ESCREVE é o frontend, ao trocar o idioma (`writeLanguageCookie`); o
 * backend (`backend/photoeditor/i18n.py`) só lê, para responder no mesmo
 * idioma. O frontend precisa do nome antes de qualquer chamada à API, para
 * desenhar a primeira tela sem esperar a rede.
 *
 * **Ao forkar:** renomeie nos DOIS lugares. O nome está repetido de propósito
 * (não dá para buscá-lo na API, que ainda não respondeu quando ele é lido), e
 * divergir faz o cookie ser escrito com um nome e procurado com outro — o
 * sintoma é o idioma simplesmente não persistir, sem erro nenhum.
 */
export const LANGUAGE_COOKIE = "photoeditor_ln";

/** Um ano. É preferência, não sessão. */
const LANGUAGE_COOKIE_MAX_AGE = 365 * 24 * 60 * 60;

/** Normaliza para um idioma suportado, ou null. */
export function normalize(lang) {
  const value = (lang || "").trim().toLowerCase().replace("_", "-");
  if (SUPPORTED.includes(value)) return value;
  const base = value.split("-")[0];
  if (base === "pt") return "pt-br";
  if (base === "en") return "en";
  return null;
}

export function readLanguageCookie(cookieString) {
  const raw = cookieString !== undefined
    ? cookieString
    : (typeof document === "undefined" ? "" : document.cookie);
  const match = (raw || "").match(new RegExp(`(?:^|; )${LANGUAGE_COOKIE}=([^;]*)`));
  return match ? normalize(decodeURIComponent(match[1])) : null;
}

/** Guarda a escolha explícita de idioma para as próximas visitas. */
export function writeLanguageCookie(lang) {
  const value = normalize(lang);
  if (!value || typeof document === "undefined") return;
  const secure = window.location.protocol === "https:" ? "; Secure" : "";
  document.cookie = `${LANGUAGE_COOKIE}=${encodeURIComponent(value)}; `
    + `Max-Age=${LANGUAGE_COOKIE_MAX_AGE}; Path=/; SameSite=Lax${secure}`;
}

/**
 * O primeiro idioma do navegador que o sistema fala, ou null.
 *
 * Devolve null — e não o fallback — porque quem chama precisa distinguir "o
 * navegador pediu inglês" de "o navegador não pediu nada que eu fale". Nos
 * dois casos a tela sairia em inglês, mas só no segundo o padrão do sistema
 * ainda tem direito de opinar.
 */
export function browserLanguage(languages) {
  const candidates = languages !== undefined
    ? languages
    : (typeof navigator === "undefined"
      ? []
      : (navigator.languages && navigator.languages.length
        ? navigator.languages
        : [navigator.language]));
  for (const c of candidates || []) {
    const n = normalize(c);
    if (n) return n;
  }
  return null;
}

export function detectBrowserLanguage(languages) {
  return browserLanguage(languages) || FALLBACK;
}

/**
 * Idioma inicial da sessão, e DE ONDE ele veio.
 *
 * O cookie vem primeiro porque é uma escolha explícita de quem usa o sistema,
 * e o navegador é a configuração do aparelho — muita gente usa o sistema
 * operacional em inglês e prefere ler em português. Entre o que a pessoa pediu
 * e o que o aparelho informa, vale o que ela pediu.
 *
 * A origem importa porque o terceiro degrau chega TARDE: o padrão do sistema é
 * configurável por deploy e vem no /api/config/, depois de a tela já ter
 * sido desenhada. Sem saber que a escolha atual foi só o fallback, adotá-lo
 * atropelaria uma preferência real.
 */
export function initialLanguage(opts = {}) {
  const fromCookie = readLanguageCookie(opts.cookie);
  if (fromCookie) return { lang: fromCookie, source: "cookie" };
  const fromBrowser = browserLanguage(opts.languages);
  if (fromBrowser) return { lang: fromBrowser, source: "browser" };
  return { lang: FALLBACK, source: "fallback" };
}

/**
 * Aplica o padrão do sistema sobre um estado já resolvido.
 *
 * Só quando nem o cookie nem o navegador responderam — é o TERCEIRO degrau da
 * ordem, não o primeiro. Aplicá-lo sempre desfaria a escolha de quem trocou o
 * idioma e a de quem tem o navegador configurado.
 */
export function withSystemDefault(state, value) {
  const normalized = normalize(value);
  if (!normalized || state.source !== "fallback") return state;
  return { lang: normalized, source: "system" };
}

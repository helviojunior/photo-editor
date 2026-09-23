import React, { createContext, useContext, useState, useCallback, useEffect } from "react";
import { LOCALES } from "./locales";
import {
  FALLBACK, initialLanguage, normalize, withSystemDefault, writeLanguageCookie,
} from "lib/language";

// A resolucao do idioma vive em lib/language.js: e decisao pura, e
// aqui ha JSX — um teste em node nao consegue carregar este arquivo.
export {
  SUPPORTED, FALLBACK, LANGUAGE_COOKIE, normalize, readLanguageCookie,
  writeLanguageCookie, browserLanguage, detectBrowserLanguage,
} from "lib/language";

export const LANGUAGE_OPTIONS = [
  { value: "en", label: "English" },
  { value: "pt-br", label: "Português (Brasil)" },
];

const I18nContext = createContext(null);

export function I18nProvider({ children }) {
  // O estado carrega a ORIGEM do idioma, e nao so o idioma: e ela que decide
  // se o padrao do sistema (que chega depois, no /api/config/) pode ou
  // nao sobrescrever o que ja esta valendo.
  const [state, setState] = useState(initialLanguage);
  const lang = state.lang;

  // Escolha explicita de quem usa o sistema: vale para a sessao e fica no
  // cookie para as proximas visitas (nao ha conta onde guardar a preferencia).
  const setLanguage = useCallback((value) => {
    const lang = normalize(value) || FALLBACK;
    writeLanguageCookie(lang);
    setState({ lang, source: "explicit" });
  }, []);

  /** Aplica o padrão do sistema, que chega pelo /api/config/.
   *
   *  Só quando nem o cookie nem o navegador responderam — é o TERCEIRO degrau
   *  da ordem, não o primeiro. Aplicá-lo sempre desfaria a escolha de quem
   *  trocou o idioma e a de quem tem o navegador configurado. */
  const adoptSystemDefault = useCallback((value) => {
    setState((s) => withSystemDefault(s, value));
  }, []);

  useEffect(() => {
    document.documentElement.setAttribute("lang", lang);
  }, [lang]);

  const t = useCallback(
    (key, fallbackText) => {
      const table = LOCALES[lang] || LOCALES[FALLBACK];
      return table[key] ?? LOCALES[FALLBACK][key] ?? fallbackText ?? key;
    },
    [lang]
  );

  // Interpolação simples: t("x.y", "…") com {placeholders}.
  const tf = useCallback(
    (key, params = {}, fallbackText) => {
      const text = t(key, fallbackText);
      return Object.keys(params).reduce(
        (acc, k) => acc.replaceAll(`{${k}}`, params[k]),
        text
      );
    },
    [t]
  );

  return (
    <I18nContext.Provider
      value={{ lang, setLanguage, adoptSystemDefault, t, tf }}>
      {children}
    </I18nContext.Provider>
  );
}

export function useI18n() {
  const ctx = useContext(I18nContext);
  if (!ctx) throw new Error("useI18n must be used within I18nProvider");
  return ctx;
}

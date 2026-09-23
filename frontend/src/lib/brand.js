import { asset, withTs } from "lib/asset";

/**
 * Identidade visual do sistema, configuravel por ambiente.
 *
 * O logo do PhotoE mora no proprio build (public/assets/logo/, baixado do
 * media.sec4us.com.br); o favicon continua remoto (padrão do ../sec_face).
 * Tudo com cache-busting por build (`asset`/`withTs`). REACT_APP_BRAND_* — do
 * .env único da raiz, como build args do Docker — sobrescrevem, se definidas.
 */
export const MEDIA_BASE = process.env.REACT_APP_MEDIA_BASE || "https://media.sec4us.com.br";

const brand = {
  name: process.env.REACT_APP_BRAND_NAME || "PhotoEditor",
  // logo = tema claro; logoDark = tema escuro. O sufixo do arquivo indica o
  // MODO em que a arte e usada, nao a cor dela (o "dark" tem texto branco).
  logo: asset(process.env.REACT_APP_BRAND_LOGO || "/assets/logo/photoe-light.png"),
  logoDark: asset(process.env.REACT_APP_BRAND_LOGO_DARK || "/assets/logo/photoe-dark.png"),
  favicon: withTs(process.env.REACT_APP_BRAND_FAVICON || `${MEDIA_BASE}/icon/favicon.png`),
  contactEmail:
    process.env.REACT_APP_BRAND_CONTACT_EMAIL || "contato@photoeditor.com.br",
  version: process.env.REACT_APP_VERSION || "1.0.0",
};

export default brand;

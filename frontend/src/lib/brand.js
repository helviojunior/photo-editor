import { asset } from "lib/asset";

/**
 * Identidade visual do sistema, configuravel por ambiente.
 *
 * Logo e favicon moram no proprio build (public/): o app desktop funciona
 * offline. Tudo com cache-busting por build (`asset`/`withTs`).
 * REACT_APP_BRAND_* no ambiente do build sobrescrevem, se definidas.
 */

const brand = {
  name: process.env.REACT_APP_BRAND_NAME || "PhotoEditor",
  // logo = tema claro; logoDark = tema escuro. O sufixo do arquivo indica o
  // MODO em que a arte e usada, nao a cor dela (o "dark" tem texto branco).
  logo: asset(process.env.REACT_APP_BRAND_LOGO || "/assets/logo/photoe-light.png"),
  logoDark: asset(process.env.REACT_APP_BRAND_LOGO_DARK || "/assets/logo/photoe-dark.png"),
  favicon: asset(process.env.REACT_APP_BRAND_FAVICON || "/favicon.png"),
  contactEmail:
    process.env.REACT_APP_BRAND_CONTACT_EMAIL || "contato@photoeditor.com.br",
  // Carimbada pelo tools/build.py (versao das Releases do GitHub).
  version: process.env.REACT_APP_VERSION || "0.0.0-dev",
};

export default brand;

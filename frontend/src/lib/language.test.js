/**
 * A ordem de idioma da sessão: cookie -> navegador -> padrão do sistema.
 *
 * São três degraus, e cada um só existe porque o anterior pode não responder —
 * o tipo de regra que passa a impressão de funcionar enquanto o primeiro
 * degrau estiver preenchido. Os casos abaixo cobrem cada degrau ganhando e
 * cada um sendo pulado.
 */
import { initialLanguage, withSystemDefault } from "./language";

/** A tela inteira: o estado inicial mais o padrão do sistema, que só chega
 *  depois, na resposta do /api/config/. */
const resolve = (cookie, languages, systemDefault) =>
  withSystemDefault(initialLanguage({ cookie, languages }), systemDefault).lang;

const withCookie = (value) => `foo=1; photoeditor_ln=${value}; bar=2`;

describe("ordem de prioridade do idioma", () => {
  test("o cookie ganha do navegador e do padrão do sistema", () => {
    expect(resolve(withCookie("pt-br"), ["en-US"], "en")).toBe("pt-br");
    expect(resolve(withCookie("en"), ["pt-BR"], "pt-br")).toBe("en");
  });

  test("sem cookie, o navegador ganha do padrão do sistema", () => {
    expect(resolve("", ["pt-BR", "en"], "en")).toBe("pt-br");
    expect(resolve("", ["en-GB"], "pt-br")).toBe("en");
  });

  test("sem cookie e sem idioma suportado no navegador, vale o sistema", () => {
    expect(resolve("", ["fr-FR", "de"], "pt-br")).toBe("pt-br");
    expect(resolve("", [], "pt-br")).toBe("pt-br");
  });

  test("ninguém responde: EN, o fallback do catálogo", () => {
    expect(resolve("", ["fr"], "")).toBe("en");
    expect(resolve("", [], "klingon")).toBe("en");
  });
});

describe("degraus que não respondem", () => {
  // Um cookie com lixo não pode travar a resolução no primeiro degrau: quem
  // edita o cookie à mão veria a tela em inglês para sempre.
  test("cookie inválido cai para o degrau seguinte", () => {
    expect(resolve(withCookie("klingon"), ["pt-BR"], "en")).toBe("pt-br");
    expect(resolve("photoeditor_ln=; x=1", ["pt-BR"], "en")).toBe("pt-br");
  });

  test("variantes e separador são normalizados", () => {
    expect(resolve(withCookie("pt_BR"), [], "en")).toBe("pt-br");
    expect(resolve("", ["pt-PT"], "en")).toBe("pt-br");
  });
});

describe("origem do idioma", () => {
  // A origem não é enfeite: é ela que autoriza (ou barra) o terceiro degrau,
  // que chega tarde, depois da tela já desenhada.
  test("é registrada em cada degrau", () => {
    expect(initialLanguage({ cookie: withCookie("en"), languages: ["pt-BR"] }).source)
      .toBe("cookie");
    expect(initialLanguage({ cookie: "", languages: ["pt-BR"] }).source).toBe("browser");
    expect(initialLanguage({ cookie: "", languages: ["fr"] }).source).toBe("fallback");
  });

  test("o padrão do sistema não desfaz uma escolha explícita", () => {
    expect(withSystemDefault({ lang: "en", source: "explicit" }, "pt-br").lang).toBe("en");
    expect(withSystemDefault({ lang: "en", source: "cookie" }, "pt-br").lang).toBe("en");
    expect(withSystemDefault({ lang: "en", source: "browser" }, "pt-br").lang).toBe("en");
    expect(withSystemDefault({ lang: "en", source: "fallback" }, "pt-br").lang).toBe("pt-br");
  });
});

/**
 * Legenda do Instagram montada com os dados do evento: o nome sai da pasta
 * do projeto, que costuma começar com a data — ela não pode virar hashtag.
 */
import { eventDate, eventName, fillCaption, hashtag } from "./caption";

describe("nome do evento", () => {
  test("tira a data do começo do nome da pasta", () => {
    expect(eventName("2026-09-20 Casamento Ana e João")).toBe("Casamento Ana e João");
    expect(eventName("20260920_Formatura")).toBe("Formatura");
  });

  test("pasta que é só a data fica como está", () => {
    expect(eventName("2026-09-20")).toBe("2026-09-20");
  });

  test("hashtag sem acento, espaço nem pontuação", () => {
    expect(hashtag("Casamento Ana e João")).toBe("#CasamentoAnaEJoao");
    expect(hashtag("  ")).toBe("");
  });
});

describe("data do evento", () => {
  test("um dia só, por extenso no idioma", () => {
    expect(eventDate("2026-09-20T15:00:00", "2026-09-20T23:00:00", "pt-br"))
      .toBe("20 de setembro de 2026");
    expect(eventDate("2026-09-20T15:00:00", null, "en")).toBe("September 20, 2026");
  });

  test("sem data de captura, vazio", () => {
    expect(eventDate(null, null, "en")).toBe("");
  });
});

describe("legenda", () => {
  const event = { name: "2026-09-20 Casamento Ana", first: "2026-09-20T15:00:00",
    last: "2026-09-20T22:00:00", photos: 120 };

  test("preenche os campos do modelo", () => {
    expect(fillCaption("{event}\n{date}\n\n{hashtag} ({photos})", event, "pt-br"))
      .toBe("Casamento Ana\n20 de setembro de 2026\n\n#CasamentoAna (120)");
  });

  test("campo vazio não deixa linhas em branco sobrando", () => {
    expect(fillCaption("{event}\n{date}\n\n{hashtag}", { ...event, first: null }, "en"))
      .toBe("Casamento Ana\n\n#CasamentoAna");
  });

  test("campo desconhecido fica como está", () => {
    expect(fillCaption("{event} {local}", event, "en")).toBe("Casamento Ana {local}");
  });
});

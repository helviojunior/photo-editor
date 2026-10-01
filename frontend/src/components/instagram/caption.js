/**
 * Legenda do "Publicar no Instagram", montada com os dados do evento.
 *
 * O modelo vem da tela Configurações > Instagram (ou o padrão do idioma) e
 * aceita os campos:
 *
 *   {event}    nome do evento — a pasta do projeto, sem a data no começo
 *   {date}     dia (ou período) das fotos, por extenso no idioma da tela
 *   {photos}   quantas fotos o evento tem
 *   {hashtag}  o nome do evento como hashtag (#CasamentoAnaEJoao)
 *
 * A data é formatada aqui, e não no backend, porque o Intl do navegador já
 * sabe escrever "20 de setembro de 2026" em cada idioma.
 */
export const CAPTION_FIELDS = ["event", "date", "photos", "hashtag"];

// "2026-09-20 Casamento", "2026_09_20-Casamento", "20260920 Casamento".
const LEADING_DATE = /^\s*\d{4}[-_.]?\d{2}[-_.]?\d{2}[\s\-_.]*/;

export function eventName(folder) {
  const name = (folder || "").trim();
  const clean = name.replace(LEADING_DATE, "").trim();
  return clean || name;
}

export function hashtag(name) {
  const words = (name || "")
    .normalize("NFD")
    .replace(/[̀-ͯ]/g, "")
    .split(/[^A-Za-z0-9]+/)
    .filter(Boolean);
  if (!words.length) return "";
  return `#${words.map((w) => w[0].toUpperCase() + w.slice(1)).join("")}`;
}

function intlLocale(lang) {
  return lang === "pt-br" ? "pt-BR" : "en-US";
}

export function eventDate(first, last, lang) {
  if (!first) return "";
  const fmt = new Intl.DateTimeFormat(intlLocale(lang),
    { day: "numeric", month: "long", year: "numeric" });
  const a = new Date(first);
  const b = last ? new Date(last) : a;
  if (fmt.format(a) === fmt.format(b)) return fmt.format(a);
  return typeof fmt.formatRange === "function"
    ? fmt.formatRange(a, b)
    : `${fmt.format(a)} – ${fmt.format(b)}`;
}

export function fillCaption(template, event, lang) {
  const name = eventName(event?.name);
  const values = {
    event: name,
    date: eventDate(event?.first, event?.last, lang),
    photos: event?.photos ? String(event.photos) : "",
    hashtag: hashtag(name),
  };
  return (template || "")
    .replace(/\{(\w+)\}/g, (match, key) => (key in values ? values[key] : match))
    // Campo vazio (ex.: fotos sem data) não deixa linha em branco sobrando.
    .split("\n").map((line) => line.trimEnd()).join("\n")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}

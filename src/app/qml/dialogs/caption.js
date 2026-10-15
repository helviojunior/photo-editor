.pragma library

// Legenda do "Publicar no Instagram", montada com os dados do evento
// (porte de components/instagram/caption.js do frontend React).
//
// O modelo vem de Configurações > Instagram (App.instagram.captionTemplate)
// e aceita os campos:
//
//   {event}    nome do evento — a pasta do projeto, sem a data no começo
//   {date}     dia (ou período) das fotos, por extenso no idioma da tela
//   {photos}   quantas fotos o evento tem
//   {hashtag}  o nome do evento como hashtag (#CasamentoAnaEJoao)
//
//   import "caption.js" as Caption
//   Caption.fillCaption(App.instagram.captionTemplate, App.instagram.eventInfo, App.language)
//
// Os nomes dos meses saem do Qt.locale() — o mesmo CLDR do Intl do
// navegador. Fica em JS puro (sem nada além do ``Qt.locale``) para os
// testes do caption.test.js antigo rodarem no node com um shim do ``Qt``.

// "2026-09-20 Casamento", "2026_09_20-Casamento", "20260920 Casamento".
var LEADING_DATE = /^\s*\d{4}[-_.]?\d{2}[-_.]?\d{2}[\s\-_.]*/;

function eventName(folder) {
    var name = String(folder || "").trim();
    var clean = name.replace(LEADING_DATE, "").trim();
    return clean || name;
}

function hashtag(name) {
    var words = String(name || "")
        .normalize("NFD")
        .replace(/[̀-ͯ]/g, "")
        .split(/[^A-Za-z0-9]+/)
        .filter(function (w) { return w !== ""; });
    if (!words.length)
        return "";
    return "#" + words.map(function (w) { return w[0].toUpperCase() + w.slice(1); }).join("");
}

// "pt_BR", "pt-br", "pt" -> português; o resto (e o vazio) -> inglês.
function isPortuguese(lang) {
    return /^pt/i.test(String(lang || ""));
}

function monthName(month, lang) {
    return Qt.locale(isPortuguese(lang) ? "pt_BR" : "en_US").monthName(month);
}

function toDate(value) {
    if (value === undefined || value === null || value === "")
        return null;
    var d = value instanceof Date ? value : new Date(value);
    return isNaN(d.getTime()) ? null : d;
}

// Partes de uma data por extenso: "20 de setembro de 2026" / "September 20, 2026".
function formatDay(d, lang, withMonth, withYear) {
    var day = String(d.getDate());
    var month = monthName(d.getMonth(), lang);
    var year = String(d.getFullYear());
    if (isPortuguese(lang)) {
        var pt = day;
        if (withMonth)
            pt += " de " + month;
        if (withYear)
            pt += " de " + year;
        return pt;
    }
    var en = withMonth ? month + " " + day : day;
    if (withYear)
        en += ", " + year;
    return en;
}

function formatDate(d, lang) {
    return formatDay(d, lang, true, true);
}

// Período como o Intl.formatRange: repete só o que muda.
//   20 – 22 de setembro de 2026        September 20 – 22, 2026
//   30 de setembro – 2 de outubro de 2026   September 30 – October 2, 2026
function formatRange(a, b, lang) {
    var sameYear = a.getFullYear() === b.getFullYear();
    var sameMonth = sameYear && a.getMonth() === b.getMonth();
    var pt = isPortuguese(lang);
    if (sameMonth) {
        if (pt)
            return a.getDate() + " – " + formatDay(b, lang, true, true);
        return formatDay(a, lang, true, false) + " – " + b.getDate() + ", " + b.getFullYear();
    }
    if (sameYear)
        return formatDay(a, lang, true, false) + " – " + formatDay(b, lang, true, true);
    return formatDate(a, lang) + " – " + formatDate(b, lang);
}

function eventDate(first, last, lang) {
    var a = toDate(first);
    if (!a)
        return "";
    var b = toDate(last) || a;
    if (b < a) {
        var t = a;
        a = b;
        b = t;
    }
    if (formatDate(a, lang) === formatDate(b, lang))
        return formatDate(a, lang);
    return formatRange(a, b, lang);
}

function fillCaption(template, event, lang) {
    var e = event || {};
    var name = eventName(e.name);
    var values = {
        event: name,
        date: eventDate(e.first, e.last, lang),
        photos: e.photos ? String(e.photos) : "",
        hashtag: hashtag(name)
    };
    return String(template || "")
        .replace(/\{(\w+)\}/g, function (match, key) {
            return Object.prototype.hasOwnProperty.call(values, key) ? values[key] : match;
        })
        // Campo vazio (ex.: fotos sem data) não deixa linha em branco sobrando.
        .split("\n").map(function (line) { return line.replace(/\s+$/, ""); }).join("\n")
        .replace(/\n{3,}/g, "\n\n")
        .trim();
}

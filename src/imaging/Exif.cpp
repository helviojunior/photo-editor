// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Exif.h"

#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace pe::imaging {

namespace {

Q_LOGGING_CATEGORY(lcExif, "photoeditor.imaging.exif")

const QByteArray kExifHeader("Exif\0\0", 6);

constexpr quint16 kTiffMagic = 42;
constexpr int kIfdEntrySize = 12;
constexpr int kMaxIfdEntries = 1000;
constexpr int kTypeAscii = 2;
constexpr int kTypeShort = 3;
constexpr int kTypeLong = 4;
constexpr int kTypeIfd = 13;

// Tags que só importam para achar (e cortar) a miniatura e os sub-IFDs.
constexpr quint16 kTagGpsIfd = 0x8825;
constexpr quint16 kTagInteropIfd = 0xA005;
constexpr quint16 kTagSubIfds = 0x014A;
constexpr quint16 kTagStripOffsets = 0x0111;
constexpr quint16 kTagThumbnailOffset = 0x0201;
constexpr quint16 kTagThumbnailLength = 0x0202;

/// Bytes por elemento de cada tipo TIFF (0 = tipo desconhecido).
int typeSize(int type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;
    case 3: case 8: return 2;
    case 4: case 9: case 11: case 13: return 4;
    case 5: case 10: case 12: return 8;
    default: return 0;
    }
}

struct Entry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    qint64 entryOffset = 0;  ///< onde a entrada de 12 bytes começa no TIFF
};

struct Ifd
{
    qint64 offset = 0;
    std::vector<Entry> entries;
    quint32 next = 0;

    qint64 end() const { return offset + 2 + qint64(entries.size()) * kIfdEntrySize + 4; }
    const Entry *find(quint16 tag) const
    {
        for (const Entry &e : entries)
            if (e.tag == tag)
                return &e;
        return nullptr;
    }
};

/// Leitor de um blob TIFF (sem o "Exif\0\0"), com checagem de limites.
class Tiff
{
public:
    explicit Tiff(const QByteArray &data) : m_data(data)
    {
        if (m_data.size() < 8)
            return;
        if (m_data.startsWith("II"))
            m_little = true;
        else if (!m_data.startsWith("MM"))
            return;
        if (u16(2) != kTiffMagic)
            return;
        m_ifd0 = u32(4);
        m_ok = true;
    }

    bool ok() const { return m_ok; }
    bool little() const { return m_little; }
    quint32 ifd0() const { return m_ifd0; }
    qint64 size() const { return m_data.size(); }

    bool has(qint64 off, qint64 len) const
    {
        return off >= 0 && len >= 0 && off <= size() && len <= size() - off;
    }

    quint16 u16(qint64 off) const
    {
        const auto *p = reinterpret_cast<const uchar *>(m_data.constData() + off);
        return m_little ? quint16(p[0] | (p[1] << 8)) : quint16((p[0] << 8) | p[1]);
    }

    quint32 u32(qint64 off) const
    {
        const auto *p = reinterpret_cast<const uchar *>(m_data.constData() + off);
        return m_little ? (quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24))
                        : ((quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]));
    }

    std::optional<Ifd> readIfd(qint64 off) const
    {
        if (off < 8 || !has(off, 2))
            return std::nullopt;
        const int n = u16(off);
        if (n > kMaxIfdEntries || !has(off + 2, qint64(n) * kIfdEntrySize + 4))
            return std::nullopt;
        Ifd ifd;
        ifd.offset = off;
        ifd.entries.reserve(n);
        for (int i = 0; i < n; ++i) {
            const qint64 e = off + 2 + qint64(i) * kIfdEntrySize;
            ifd.entries.push_back({u16(e), u16(e + 2), u32(e + 4), e});
        }
        ifd.next = u32(off + 2 + qint64(n) * kIfdEntrySize);
        return ifd;
    }

    /// Tamanho do valor em bytes (-1 = tipo desconhecido).
    static qint64 valueSize(const Entry &e)
    {
        const int sz = typeSize(e.type);
        return sz ? qint64(sz) * e.count : -1;
    }

    /// Onde o valor está: dentro da entrada (<= 4 bytes) ou no offset dela.
    qint64 valueOffset(const Entry &e) const
    {
        return valueSize(e) <= 4 ? e.entryOffset + 8 : qint64(u32(e.entryOffset + 8));
    }

    /// [início, fim) do valor fora da entrada; vazio se estiver dentro dela.
    std::optional<std::pair<qint64, qint64>> outOfLine(const Entry &e) const
    {
        const qint64 sz = valueSize(e);
        if (sz <= 4)
            return std::nullopt;
        const qint64 off = valueOffset(e);
        return std::make_pair(off, off + sz);
    }

    std::optional<quint32> uintValue(const Entry &e) const
    {
        if (e.count < 1)
            return std::nullopt;
        if (e.type == kTypeShort)
            return u16(e.entryOffset + 8);
        if (e.type == kTypeLong || e.type == kTypeIfd)
            return u32(e.entryOffset + 8);
        return std::nullopt;
    }

    /// Texto ASCII (ou UNDEFINED) — os bytes crus, em Latin-1.
    QString text(const Entry &e) const
    {
        if (e.type != kTypeAscii && e.type != 7)
            return {};
        const qint64 off = valueOffset(e);
        if (!has(off, e.count))
            return {};
        return QString::fromLatin1(m_data.constData() + off, qsizetype(e.count));
    }

    /// RATIONAL/SRATIONAL (ou SHORT/LONG) como double; nullopt se ilegível.
    std::optional<double> number(const Entry &e) const
    {
        if (e.count < 1)
            return std::nullopt;
        if (e.type == 5 || e.type == 10) {
            const qint64 off = valueOffset(e);
            if (!has(off, 8))
                return std::nullopt;
            const quint32 num = u32(off), den = u32(off + 4);
            if (den == 0)
                return std::nullopt;
            if (e.type == 10)
                return double(qint32(num)) / double(qint32(den));
            return double(num) / double(den);
        }
        if (const auto v = uintValue(e))
            return double(*v);
        return std::nullopt;
    }

    QByteArray rawEntry(const Entry &e) const
    {
        return m_data.mid(e.entryOffset, kIfdEntrySize);
    }

private:
    const QByteArray &m_data;
    bool m_ok = false;
    bool m_little = false;
    quint32 m_ifd0 = 0;
};

QByteArray stripHeader(const QByteArray &exif)
{
    return exif.startsWith(kExifHeader) ? exif.mid(kExifHeader.size()) : exif;
}

/// ``str(x).strip('\x00 ')``.
QString stripNulSpace(const QString &s)
{
    qsizetype b = 0;
    qsizetype e = s.size();
    auto junk = [](QChar c) { return c == QChar(0) || c == QLatin1Char(' '); };
    while (b < e && junk(s.at(b)))
        ++b;
    while (e > b && junk(s.at(e - 1)))
        --e;
    return s.mid(b, e - b);
}

/// ``software.encode('ascii', 'replace')``: cada code point fora do ASCII vira '?'.
QByteArray asciiReplace(const QString &s)
{
    QByteArray out;
    for (char32_t cp : s.toUcs4())
        out.append(cp < 0x80 ? char(cp) : '?');
    return out;
}

/// Escreve inteiros na ordem de bytes do TIFF.
class Writer
{
public:
    explicit Writer(bool little) : m_little(little) {}

    QByteArray u16(quint16 v) const
    {
        char b[2];
        if (m_little) {
            b[0] = char(v & 0xFF);
            b[1] = char(v >> 8);
        } else {
            b[0] = char(v >> 8);
            b[1] = char(v & 0xFF);
        }
        return QByteArray(b, 2);
    }

    QByteArray u32(quint32 v) const
    {
        char b[4];
        for (int i = 0; i < 4; ++i)
            b[m_little ? i : 3 - i] = char((v >> (8 * i)) & 0xFF);
        return QByteArray(b, 4);
    }

    static void patch(QByteArray &buf, qint64 off, const QByteArray &bytes)
    {
        buf.replace(off, bytes.size(), bytes);
    }

private:
    bool m_little;
};

void padEven(QByteArray &buf)
{
    if (buf.size() % 2)
        buf.append('\0');
}

/// Entrada nova: o valor já na ordem de bytes do TIFF.
struct OutEntry
{
    quint16 tag;
    quint16 type;
    quint32 count;
    QByteArray value;
};

OutEntry shortEntry(const Writer &w, quint16 tag, quint16 v)
{
    return {tag, quint16(kTypeShort), 1, w.u16(v)};
}

OutEntry asciiEntry(quint16 tag, const QByteArray &text)
{
    QByteArray v = text;
    v.append('\0');
    return {tag, quint16(kTypeAscii), quint32(v.size()), v};
}

/// Grava ``entries`` (ordenadas pela tag) como um IFD no fim de ``buf``, com os
/// valores grandes logo depois. ``valueFixups`` recebe, por índice, o offset do
/// campo de valor de cada entrada (para apontar sub-IFDs depois). Devolve o
/// offset do IFD.
qint64 appendIfd(QByteArray &buf, const Writer &w, std::vector<OutEntry> entries,
                 std::vector<std::pair<quint16, qint64>> *valueFields = nullptr)
{
    std::stable_sort(entries.begin(), entries.end(),
                     [](const OutEntry &a, const OutEntry &b) { return a.tag < b.tag; });
    padEven(buf);
    const qint64 ifdOff = buf.size();
    qint64 dataOff = ifdOff + 2 + qint64(entries.size()) * kIfdEntrySize + 4;
    QByteArray data;
    buf.append(w.u16(quint16(entries.size())));
    for (const OutEntry &e : entries) {
        buf.append(w.u16(e.tag));
        buf.append(w.u16(e.type));
        buf.append(w.u32(e.count));
        if (valueFields)
            valueFields->emplace_back(e.tag, buf.size());
        if (e.value.size() <= 4) {
            QByteArray v = e.value;
            v.append(QByteArray(4 - v.size(), '\0'));
            buf.append(v);
        } else {
            buf.append(w.u32(quint32(dataOff + data.size())));
            data.append(e.value);
            if (data.size() % 2)
                data.append('\0');
        }
    }
    buf.append(w.u32(0));
    buf.append(data);
    return ifdOff;
}

/// TIFF do zero: IFD0 e, se houver, o IFD Exif. Big-endian, como o piexif.
QByteArray buildTiff(std::vector<OutEntry> ifd0, const std::vector<OutEntry> &exifIfd)
{
    const Writer w(false);
    QByteArray buf("MM", 2);
    buf.append(w.u16(kTiffMagic));
    buf.append(w.u32(8));
    if (!exifIfd.empty())
        ifd0.push_back({quint16(kExifIfdPointer), quint16(kTypeLong), 1, w.u32(0)});
    std::vector<std::pair<quint16, qint64>> fields;
    appendIfd(buf, w, ifd0, &fields);
    if (!exifIfd.empty()) {
        const qint64 exifOff = appendIfd(buf, w, exifIfd);
        for (const auto &[tag, field] : fields)
            if (tag == kExifIfdPointer)
                Writer::patch(buf, field, w.u32(quint32(exifOff)));
    }
    return buf;
}

/// Fim do que um IFD ocupa (a estrutura e os valores fora dela).
qint64 ifdExtent(const Tiff &t, const Ifd &ifd)
{
    qint64 end = ifd.end();
    for (const Entry &e : ifd.entries)
        if (const auto range = t.outOfLine(e))
            end = std::max(end, range->second);
    return end;
}

/// Offset a partir do qual o TIFF pode ser cortado sem perder nada além do
/// IFD1 e da miniatura — ou o tamanho do TIFF, se a miniatura não está na
/// cauda (ou há algo que não sabemos medir).
qint64 thumbnailCut(const Tiff &t, const Ifd &ifd0)
{
    if (ifd0.next == 0)
        return t.size();
    const auto ifd1 = t.readIfd(ifd0.next);
    if (!ifd1 || ifd1->next != 0 || ifd1->find(kTagStripOffsets))
        return t.size();

    // O que fica: IFD0, Exif, GPS, Interop e os valores deles.
    qint64 keepEnd = ifdExtent(t, ifd0);
    for (const Entry &e : ifd0.entries)
        if (e.tag == kTagSubIfds || e.type == kTypeIfd)
            return t.size();  // sub-IFDs que não percorremos: não arrisca
    auto walk = [&](const Ifd &parent, quint16 tag) -> std::optional<Ifd> {
        const Entry *p = parent.find(tag);
        if (!p)
            return std::nullopt;
        const auto off = t.uintValue(*p);
        if (!off)
            return std::nullopt;
        auto sub = t.readIfd(*off);
        if (sub)
            keepEnd = std::max(keepEnd, ifdExtent(t, *sub));
        return sub;
    };
    if (const auto exif = walk(ifd0, quint16(kExifIfdPointer)))
        walk(*exif, kTagInteropIfd);
    walk(ifd0, kTagGpsIfd);

    // O que sai: IFD1, os valores dele e o JPEG da miniatura.
    qint64 start = ifd1->offset;
    qint64 end = ifdExtent(t, *ifd1);
    for (const Entry &e : ifd1->entries)
        if (const auto range = t.outOfLine(e))
            start = std::min(start, range->first);
    const Entry *thumbOff = ifd1->find(kTagThumbnailOffset);
    const Entry *thumbLen = ifd1->find(kTagThumbnailLength);
    if (thumbOff && thumbLen) {
        const auto off = t.uintValue(*thumbOff);
        const auto len = t.uintValue(*thumbLen);
        if (off && len) {
            start = std::min<qint64>(start, *off);
            end = std::max<qint64>(end, qint64(*off) + *len);
        }
    }
    // Até 3 bytes de enchimento no fim são tolerados.
    if (start >= keepEnd && start >= 8 && end <= t.size() && end >= t.size() - 3)
        return start;
    return t.size();
}

/// EXIF mínimo: Orientation = 1, Software e (se houver) as três tags de data.
QByteArray minimalExif(const QByteArray &software, const ExifInfo &info)
{
    const Writer w(false);
    std::vector<OutEntry> ifd0{shortEntry(w, quint16(kExifOrientation), 1)};
    if (!software.isEmpty())
        ifd0.push_back(asciiEntry(quint16(kExifSoftware), software));
    std::vector<OutEntry> exifIfd;
    auto addText = [&](int tag, const QString &value) {
        if (!value.isEmpty())
            exifIfd.push_back(asciiEntry(quint16(tag), stripNulSpace(value).toLatin1()));
    };
    addText(kExifDateTimeOriginal, info.dateTimeOriginal);
    addText(kExifOffsetTimeOriginal, info.offsetTimeOriginal);
    addText(kExifSubsecTimeOriginal, info.subsecTimeOriginal);
    return kExifHeader + buildTiff(ifd0, exifIfd);
}

/// O TIFF original intacto (menos a miniatura na cauda) + um IFD0 novo no fim.
std::optional<QByteArray> rewriteExif(const QByteArray &tiff, const QByteArray &software)
{
    const Tiff t(tiff);
    if (!t.ok())
        return std::nullopt;
    const auto ifd0 = t.readIfd(t.ifd0());
    if (!ifd0)
        return std::nullopt;

    const Writer w(t.little());
    QByteArray out = tiff.left(thumbnailCut(t, *ifd0));
    padEven(out);

    // Entradas do IFD0 novo: as originais copiadas byte a byte (offsets
    // absolutos continuam válidos), Orientation trocada e Software novo.
    struct Raw
    {
        quint16 tag;
        QByteArray bytes;
    };
    std::vector<Raw> entries;
    for (const Entry &e : ifd0->entries) {
        if (e.tag == kExifOrientation || (e.tag == kExifSoftware && !software.isEmpty()))
            continue;
        entries.push_back({e.tag, t.rawEntry(e)});
    }
    auto makeEntry = [&](quint16 tag, quint16 type, quint32 count, const QByteArray &field) {
        QByteArray b = w.u16(tag) + w.u16(type) + w.u32(count) + field;
        b.append(QByteArray(kIfdEntrySize - b.size(), '\0'));
        return Raw{tag, b};
    };
    entries.push_back(makeEntry(quint16(kExifOrientation), quint16(kTypeShort), 1, w.u16(1)));
    if (!software.isEmpty()) {
        QByteArray value = software;
        value.append('\0');
        QByteArray field;
        if (value.size() <= 4) {
            field = value;
        } else {
            field = w.u32(quint32(out.size()));
            out.append(value);
            padEven(out);
        }
        entries.push_back(makeEntry(quint16(kExifSoftware), quint16(kTypeAscii), quint32(value.size()), field));
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Raw &a, const Raw &b) { return a.tag < b.tag; });

    const qint64 ifdOff = out.size();
    if (ifdOff > std::numeric_limits<quint32>::max())
        return std::nullopt;
    out.append(w.u16(quint16(entries.size())));
    for (const Raw &r : entries)
        out.append(r.bytes);
    out.append(w.u32(0));  // sem IFD1
    Writer::patch(out, 4, w.u32(quint32(ifdOff)));
    return out;
}

} // namespace

QDateTime capturedAtFromExif(const QString &dateTimeOriginal, const QString &subsec, const QString &offset)
{
    static const QRegularExpression kDateRe(
        QStringLiteral("^(\\d{4}):(\\d{1,2}):(\\d{1,2}) (\\d{1,2}):(\\d{1,2}):(\\d{1,2})$"));
    const QString raw = stripNulSpace(dateTimeOriginal);
    if (raw.isEmpty())
        return {};
    const auto m = kDateRe.match(raw);
    if (!m.hasMatch())
        return {};
    const QDate date(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    const int h = m.captured(4).toInt();
    const int mi = m.captured(5).toInt();
    const int s = m.captured(6).toInt();
    if (!date.isValid() || h > 23 || mi > 59 || s > 59)
        return {};

    // "12" = 0,12 s; "123" = 0,123 s — a subsec é a parte fracionária. O
    // QTime guarda milissegundos: os dígitos além do terceiro se perdem.
    int ms = 0;
    const QString sub = stripNulSpace(subsec);
    static const QRegularExpression kDigits(QStringLiteral("^\\d+$"));
    if (kDigits.match(sub).hasMatch())
        ms = (sub + QStringLiteral("000")).left(3).toInt();
    const QTime time(h, mi, s, ms);

    const QString off = stripNulSpace(offset);
    if (off.size() == 6 && (off[0] == QLatin1Char('+') || off[0] == QLatin1Char('-')) && off[3] == QLatin1Char(':')) {
        bool okH = false;
        bool okM = false;
        const int oh = off.mid(1, 2).toInt(&okH);
        const int om = off.mid(4, 2).toInt(&okM);
        const qint64 secs = (qint64(oh) * 60 + om) * 60;
        // timezone() do Python só aceita |offset| < 24 h.
        if (okH && okM && oh >= 0 && om >= 0 && secs < 24 * 3600) {
            const int sign = off[0] == QLatin1Char('+') ? 1 : -1;
            return QDateTime(date, time, QTimeZone::UTC).addSecs(-sign * secs);
        }
    }
    // Sem offset gravado: a câmera marca a hora local do evento.
    return QDateTime(date, time, QTimeZone::LocalTime).toUTC();
}

ExifInfo parseExif(const QByteArray &exif)
{
    ExifInfo info;
    const QByteArray tiff = stripHeader(exif);
    const Tiff t(tiff);
    if (!t.ok())
        return info;
    const auto ifd0 = t.readIfd(t.ifd0());
    if (!ifd0)
        return info;
    info.valid = true;
    info.hasIfd1 = ifd0->next != 0;
    if (const Entry *sw = ifd0->find(quint16(kExifSoftware)))
        info.software = stripNulSpace(t.text(*sw));
    if (const Entry *e = ifd0->find(quint16(kExifMake)))
        info.make = stripNulSpace(t.text(*e));
    if (const Entry *e = ifd0->find(quint16(kExifModel)))
        info.model = stripNulSpace(t.text(*e));

    if (const Entry *o = ifd0->find(quint16(kExifOrientation))) {
        const auto v = t.uintValue(*o);
        if (v && *v >= 1 && *v <= 8)
            info.orientation = int(*v);
    }
    if (const Entry *p = ifd0->find(quint16(kExifIfdPointer))) {
        const auto off = t.uintValue(*p);
        const auto sub = off ? t.readIfd(*off) : std::nullopt;
        if (sub) {
            auto text = [&](int tag) {
                const Entry *e = sub->find(quint16(tag));
                return e ? stripNulSpace(t.text(*e)) : QString();
            };
            info.dateTimeOriginal = text(kExifDateTimeOriginal);
            info.subsecTimeOriginal = text(kExifSubsecTimeOriginal);
            info.offsetTimeOriginal = text(kExifOffsetTimeOriginal);
            info.capturedAt = capturedAtFromExif(info.dateTimeOriginal, info.subsecTimeOriginal,
                                                 info.offsetTimeOriginal);
            info.lensModel = text(kExifLensModel);
            auto number = [&](int tag) {
                const Entry *e = sub->find(quint16(tag));
                const auto v = e ? t.number(*e) : std::nullopt;
                return v && std::isfinite(*v) && *v > 0 ? *v : 0.0;
            };
            info.exposureTime = number(kExifExposureTime);
            info.fNumber = number(kExifFNumber);
            info.focalLength = number(kExifFocalLength);
            info.iso = int(std::lround(std::min(number(kExifIsoSpeed), 1e7)));
        }
    }
    return info;
}

QString formatShutter(double seconds)
{
    if (!(seconds > 0))
        return {};
    if (seconds < 0.5)
        return QStringLiteral("1/%1").arg(std::lround(1.0 / seconds));
    const double r = std::nearbyint(seconds * 10.0) / 10.0;
    return (r == std::floor(r) ? QString::number(qint64(r)) : QString::number(r, 'f', 1)) + u's';
}

QString formatAperture(double fNumber)
{
    if (!(fNumber > 0))
        return {};
    const double r = std::nearbyint(fNumber * 10.0) / 10.0;
    return QStringLiteral("f/") + (r == std::floor(r) ? QString::number(qint64(r)) : QString::number(r, 'f', 1));
}

QString cameraName(const QString &make, const QString &model)
{
    const QString mk = make.simplified(), md = model.simplified();
    if (mk.isEmpty())
        return md;
    if (md.isEmpty())
        return mk;
    // "NIKON CORPORATION" + "NIKON D850" -> "NIKON D850"
    const QString brand = mk.section(u' ', 0, 0);
    if (md.startsWith(brand, Qt::CaseInsensitive))
        return md;
    return mk + u' ' + md;
}

QByteArray exifForPublication(const QByteArray &exif, const QString &software)
{
    const QByteArray sw = asciiReplace(software);
    if (exif.isEmpty())
        return minimalExif(sw, {});
    const auto rewritten = rewriteExif(stripHeader(exif), sw);
    if (!rewritten) {
        qCWarning(lcExif) << "Could not read EXIF; exporting without the original.";
        return minimalExif(sw, {});
    }
    if (kExifHeader.size() + rewritten->size() <= kMaxApp1Payload)
        return kExifHeader + *rewritten;
    qCWarning(lcExif) << "EXIF too large for APP1 (" << rewritten->size()
                      << "bytes); keeping only Software and capture time.";
    return minimalExif(sw, parseExif(exif));
}

QByteArray insertExif(const QByteArray &jpeg, const QByteArray &exif)
{
    if (exif.size() > kMaxApp1Payload || jpeg.size() < 4 || uchar(jpeg[0]) != 0xFF || uchar(jpeg[1]) != 0xD8)
        return {};
    const auto *d = reinterpret_cast<const uchar *>(jpeg.constData());
    const qint64 n = jpeg.size();

    QByteArray app1("\xFF\xE1", 2);
    const int len = int(exif.size()) + 2;
    app1.append(char(len >> 8));
    app1.append(char(len & 0xFF));
    app1.append(exif);

    QByteArray out = jpeg.left(2);
    bool inserted = false;
    bool first = true;
    qint64 pos = 2;
    while (pos + 4 <= n && d[pos] == 0xFF) {
        while (pos + 1 < n && d[pos + 1] == 0xFF)  // bytes de enchimento
            ++pos;
        const uchar marker = d[pos + 1];
        if (marker == 0xDA || marker == 0xD9 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01)
            break;
        if (pos + 4 > n)
            break;
        const qint64 segLen = (qint64(d[pos + 2]) << 8) | d[pos + 3];
        if (segLen < 2 || pos + 2 + segLen > n)
            return {};
        const QByteArray seg = jpeg.mid(pos, 2 + segLen);
        const bool isExif = marker == 0xE1 && seg.mid(4).startsWith(kExifHeader);
        if (!inserted && !(first && marker == 0xE0)) {
            out.append(app1);
            inserted = true;
        }
        if (!isExif)
            out.append(seg);
        if (first && marker == 0xE0) {
            out.append(app1);
            inserted = true;
        }
        first = false;
        pos += 2 + segLen;
    }
    if (!inserted)
        out.append(app1);
    out.append(jpeg.mid(pos));
    return out;
}

} // namespace pe::imaging

// SPDX-License-Identifier: BSD-2-Clause
// EXIF, leitura de JPEG e formato de publicação (Exif, ImageIO, Publish).
//
// Fixtures em tests/data/exif/ geradas com piexif/Pillow (quadrantes: topo-esq
// vermelho, topo-dir verde, baixo-esq azul, baixo-dir branco):
//   mm_o6_thumb.jpg  640x480, MM, Orientation 6, offset -03:00, subsec 123,
//                    MakerNote, GPS e miniatura no IFD1
//   ii_o8.jpg        64x48, II, Orientation 8, sem offset, subsec 45
//   no_exif.jpg      64x48, sem EXIF
//   o1..o8.jpg       64x48, só Orientation
//   big_exif.bin     TIFF de ~64 KB (MakerNote enorme) que não cabe num APP1
// Com PHOTOEDITOR_EXIF_DUMP_DIR definido, os JPEGs de publicação são gravados
// lá para conferência externa (piexif).
#include "imaging/Exif.h"
#include "imaging/ImageIO.h"
#include "imaging/Publish.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <opencv2/core.hpp>

#include <array>

using namespace pe::imaging;

namespace {

QString data(const QString &name)
{
    return QStringLiteral(PHOTOEDITOR_TEST_DATA "/") + name;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

enum Color { Red, Green, Blue, White, Other };

Color colorAt(const cv::Mat &rgb, double fx, double fy)
{
    const cv::Vec3b p = rgb.at<cv::Vec3b>(int(fy * rgb.rows), int(fx * rgb.cols));
    auto hi = [](uchar v) { return v > 200; };
    auto lo = [](uchar v) { return v < 60; };
    if (hi(p[0]) && hi(p[1]) && hi(p[2]))
        return White;
    if (hi(p[0]) && lo(p[1]) && lo(p[2]))
        return Red;
    if (lo(p[0]) && hi(p[1]) && lo(p[2]))
        return Green;
    if (lo(p[0]) && lo(p[1]) && hi(p[2]))
        return Blue;
    return Other;
}

/// Cantos (TL, TR, BL, BR), amostrados a 1/4 e 3/4.
std::array<Color, 4> corners(const cv::Mat &rgb)
{
    return {colorAt(rgb, 0.25, 0.25), colorAt(rgb, 0.75, 0.25), colorAt(rgb, 0.25, 0.75),
            colorAt(rgb, 0.75, 0.75)};
}

/// Marcadores do JPEG até o SOS (inclusive).
QList<int> markers(const QByteArray &jpeg)
{
    QList<int> out;
    int pos = 2;
    while (pos + 4 <= jpeg.size() && uchar(jpeg[pos]) == 0xFF) {
        const int m = uchar(jpeg[pos + 1]);
        out << m;
        if (m == 0xDA)
            break;
        pos += 2 + ((uchar(jpeg[pos + 2]) << 8) | uchar(jpeg[pos + 3]));
    }
    return out;
}

void dump(const QString &name, const QByteArray &bytes)
{
    const QString dir = qEnvironmentVariable("PHOTOEDITOR_EXIF_DUMP_DIR");
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + name);
    if (f.open(QIODevice::WriteOnly))
        f.write(bytes);
}

const QString kSoftware = QStringLiteral("PhotoEditor 1.2.3 (https://example.org/pe)");

} // namespace

class ExifTest : public QObject
{
    Q_OBJECT
private slots:
    void capturedAtParsing();
    void readInfoFixtures();
    void readInfoNonAsciiPath();
    void orientationTranspose_data();
    void orientationTranspose();
    void draftAndThumbnailSizes();
    void loadRgbReduced();
    void publicationExif_data();
    void publicationExif();
    void publicationExifFallbacks();
    void insertExifReplaces();
    void publishGeometry();
    void publishLoadDraft();
    void publishEncode();
    void derivatives();
};

void ExifTest::capturedAtParsing()
{
    const QDateTime utc(QDate(2024, 5, 17), QTime(17, 3, 22, 123), QTimeZone::UTC);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "123", "-03:00"), utc);
    QCOMPARE(capturedAtFromExif(QString::fromLatin1("2024:05:17 14:03:22\0", 20), QStringLiteral(" 123 "), "-03:00"),
             utc);
    // "12" = 0,12 s; dígitos além do milissegundo se perdem.
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "12", "+00:00").time().msec(), 120);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "1234567", "+00:00").time().msec(), 123);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "abc", "+00:00").time().msec(), 0);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "", "+05:30"),
             QDateTime(QDate(2024, 5, 17), QTime(8, 33, 22), QTimeZone::UTC));

    // Sem offset (ou offset inválido): hora local do sistema.
    const QDateTime local = QDateTime(QDate(2024, 5, 17), QTime(14, 3, 22), QTimeZone::LocalTime).toUTC();
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "", ""), local);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "", "-0300"), local);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "", "+24:00"), local);
    QCOMPARE(capturedAtFromExif("2024:05:17 14:03:22", "", "").timeSpec(), Qt::UTC);

    QVERIFY(!capturedAtFromExif("", "", "").isValid());
    QVERIFY(!capturedAtFromExif("0000:00:00 00:00:00", "", "").isValid());
    QVERIFY(!capturedAtFromExif("2024-05-17 14:03:22", "", "").isValid());
    QVERIFY(!capturedAtFromExif("2024:02:30 10:00:00", "", "").isValid());
}

void ExifTest::readInfoFixtures()
{
    // sample.jpg: EXIF MM com IFD1/miniatura, sem Orientation nem data.
    auto info = readInfo(data("sample.jpg"));
    QVERIFY(info);
    QCOMPARE(info->width, 547);
    QCOMPARE(info->height, 365);
    QCOMPARE(info->orientation, 1);
    QVERIFY(!info->capturedAt.isValid());

    info = readInfo(data("exif/mm_o6_thumb.jpg"));
    QVERIFY(info);
    QCOMPARE(info->width, 480);  // 640x480 gravado, giro de 90 graus
    QCOMPARE(info->height, 640);
    QCOMPARE(info->orientation, 6);
    QCOMPARE(info->capturedAt, QDateTime(QDate(2024, 5, 17), QTime(17, 3, 22, 123), QTimeZone::UTC));

    info = readInfo(data("exif/ii_o8.jpg"));
    QVERIFY(info);
    QCOMPARE(info->width, 48);
    QCOMPARE(info->height, 64);
    QCOMPARE(info->orientation, 8);
    QCOMPARE(info->capturedAt,
             QDateTime(QDate(2023, 12, 31), QTime(23, 59, 58, 450), QTimeZone::LocalTime).toUTC());

    info = readInfo(data("exif/no_exif.jpg"));
    QVERIFY(info);
    QCOMPARE(info->width, 64);
    QCOMPARE(info->height, 48);
    QCOMPARE(info->orientation, 1);
    QVERIFY(!info->capturedAt.isValid());

    QVERIFY(!readInfo(data("exif/big_exif.bin")));
    QVERIFY(!readInfo(data("exif/missing.jpg")));
    QVERIFY(isJpeg(data("sample.jpg")));
    QVERIFY(!isJpeg(data("exif/big_exif.bin")));
    QVERIFY(!isJpeg(data("exif")));
}

void ExifTest::readInfoNonAsciiPath()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.filePath(QStringLiteral("evento ção 写真"));
    QVERIFY(QDir().mkpath(dir));
    const QString path = dir + QStringLiteral("/fotó.JPG");
    QVERIFY(QFile::copy(data("exif/mm_o6_thumb.jpg"), path));
    QVERIFY(isJpeg(path));
    const auto info = readInfo(path);
    QVERIFY(info);
    QCOMPARE(info->orientation, 6);
    const cv::Mat img = loadRgb(path);
    QCOMPARE(img.cols, 480);
    QCOMPARE(img.rows, 640);
}

void ExifTest::orientationTranspose_data()
{
    QTest::addColumn<int>("orientation");
    QTest::addColumn<QList<int>>("expected");  // TL, TR, BL, BR
    // Gravado: TL vermelho, TR verde, BL azul, BR branco (64x48).
    QTest::newRow("1") << 1 << QList<int>{Red, Green, Blue, White};
    QTest::newRow("2 flip LR") << 2 << QList<int>{Green, Red, White, Blue};
    QTest::newRow("3 rot180") << 3 << QList<int>{White, Blue, Green, Red};
    QTest::newRow("4 flip TB") << 4 << QList<int>{Blue, White, Red, Green};
    QTest::newRow("5 transpose") << 5 << QList<int>{Red, Blue, Green, White};
    QTest::newRow("6 cw") << 6 << QList<int>{Blue, Red, White, Green};
    QTest::newRow("7 transverse") << 7 << QList<int>{White, Green, Blue, Red};
    QTest::newRow("8 ccw") << 8 << QList<int>{Green, White, Red, Blue};
}

void ExifTest::orientationTranspose()
{
    QFETCH(int, orientation);
    QFETCH(QList<int>, expected);
    const QString path = data(QStringLiteral("exif/o%1.jpg").arg(orientation));
    const auto info = readInfo(path);
    QVERIFY(info);
    QCOMPARE(info->orientation, orientation);
    const cv::Mat img = loadRgb(path);
    QCOMPARE(img.type(), CV_8UC3);
    const bool swapped = orientation >= 5;
    QCOMPARE(img.cols, swapped ? 48 : 64);
    QCOMPARE(img.rows, swapped ? 64 : 48);
    QCOMPARE(info->width, img.cols);
    QCOMPARE(info->height, img.rows);
    const auto c = corners(img);
    QCOMPARE((QList<int>{c[0], c[1], c[2], c[3]}), expected);
}

void ExifTest::draftAndThumbnailSizes()
{
    // draft do Pillow: min(w // pedido, h // pedido), arredondado para 8/4/2/1.
    QCOMPARE(draftScale(6000, 4000, 1600), 2);
    QCOMPARE(draftScale(6000, 4000, 240), 8);
    QCOMPARE(draftScale(6000, 4000, 1000), 4);
    QCOMPARE(draftScale(640, 480, 100), 4);
    QCOMPARE(draftScale(640, 480, 1600), 1);
    QCOMPARE(draftScale(640, 480, 0), 1);

    // thumbnail do Pillow: proporção arredondada, nunca amplia.
    QCOMPARE(thumbnailSize({3000, 2000}, 1600), cv::Size(1600, 1067));
    QCOMPARE(thumbnailSize({2000, 3000}, 1600), cv::Size(1067, 1600));
    QCOMPARE(thumbnailSize({1600, 1067}, 240), cv::Size(240, 160));
    QCOMPARE(thumbnailSize({120, 160}, 100), cv::Size(75, 100));
    QCOMPARE(thumbnailSize({640, 480}, 1600), cv::Size(640, 480));
    QCOMPARE(thumbnailSize({1000, 1}, 100), cv::Size(100, 1));
}

void ExifTest::loadRgbReduced()
{
    // 640x480, Orientation 6: draft 1/4 -> 160x120 -> girada 120x160 -> 75x100.
    const cv::Mat img = loadRgb(data("exif/mm_o6_thumb.jpg"), 100);
    QCOMPARE(img.cols, 75);
    QCOMPARE(img.rows, 100);
    const auto c = corners(img);
    QCOMPARE((QList<int>{c[0], c[1], c[2], c[3]}), (QList<int>{Blue, Red, White, Green}));

    const cv::Mat dec = decodeRgb(readAll(data("exif/mm_o6_thumb.jpg")), 4);
    QCOMPARE(dec.cols, 160);
    QCOMPARE(dec.rows, 120);
    QVERIFY(decodeRgb(QByteArray("not a jpeg")).empty());
    QVERIFY(loadRgb(data("exif/missing.jpg")).empty());
}

void ExifTest::publicationExif_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<bool>("little");
    QTest::addColumn<QString>("dateTimeOriginal");
    QTest::addColumn<bool>("thumbCut");
    QTest::newRow("sample") << "sample.jpg" << false << QString() << true;
    QTest::newRow("mm_o6_thumb") << "exif/mm_o6_thumb.jpg" << false << "2024:05:17 14:03:22" << true;
    QTest::newRow("ii_o8") << "exif/ii_o8.jpg" << true << "2023:12:31 23:59:58" << false;
}

void ExifTest::publicationExif()
{
    QFETCH(QString, file);
    QFETCH(bool, little);
    QFETCH(QString, dateTimeOriginal);
    QFETCH(bool, thumbCut);

    const auto header = readJpegHeader(data(file));
    QVERIFY(header);
    const QByteArray original = header->exif;
    QVERIFY(original.startsWith(QByteArray("Exif\0\0", 6)));
    const ExifInfo before = parseExif(original);
    QVERIFY(before.valid);

    const QByteArray out = exifForPublication(original, kSoftware);
    QVERIFY(out.startsWith(QByteArray("Exif\0\0", 6)));
    QCOMPARE(out.mid(6, 2), little ? QByteArray("II") : QByteArray("MM"));
    QVERIFY(out.size() <= kMaxApp1Payload);

    const ExifInfo after = parseExif(out);
    QVERIFY(after.valid);
    QCOMPARE(after.orientation, 1);
    QCOMPARE(after.software, kSoftware);
    QVERIFY(!after.hasIfd1);
    QCOMPARE(after.dateTimeOriginal, dateTimeOriginal);
    QCOMPARE(after.capturedAt, before.capturedAt);
    QCOMPARE(after.subsecTimeOriginal, before.subsecTimeOriginal);
    QCOMPARE(after.offsetTimeOriginal, before.offsetTimeOriginal);
    if (thumbCut)
        QVERIFY2(out.size() < original.size() - 1000, qPrintable(QString::number(out.size())));

    if (file.contains(QLatin1String("mm_o6"))) {
        // MakerNote e o resto do TIFF original seguem lá, intactos.
        QByteArray makerNote;
        for (int i = 0; i < 4; ++i)
            for (int b = 0; b < 256; ++b)
                makerNote.append(char(b));
        QVERIFY(out.contains(makerNote));
        QVERIFY(out.contains("TestCam"));
    }

    // Idempotente: reescrever a saída mantém tudo.
    const ExifInfo again = parseExif(exifForPublication(out, kSoftware));
    QCOMPARE(again.orientation, 1);
    QCOMPARE(again.software, kSoftware);
    QCOMPARE(again.capturedAt, before.capturedAt);

    // Software vazio: o original fica.
    const ExifInfo keep = parseExif(exifForPublication(original, QString()));
    QCOMPARE(keep.software, before.software);
    QCOMPARE(keep.orientation, 1);
}

void ExifTest::publicationExifFallbacks()
{
    // Sem EXIF: só Orientation e Software.
    ExifInfo info = parseExif(exifForPublication({}, kSoftware));
    QVERIFY(info.valid);
    QCOMPARE(info.orientation, 1);
    QCOMPARE(info.software, kSoftware);
    QVERIFY(!info.capturedAt.isValid());

    // Ilegível: idem.
    info = parseExif(exifForPublication(QByteArray("Exif\0\0garbage!!", 15), kSoftware));
    QVERIFY(info.valid);
    QCOMPARE(info.software, kSoftware);

    // Não ASCII vira '?' (encode('ascii', 'replace')).
    info = parseExif(exifForPublication({}, QStringLiteral("Edição 写 1.0")));
    QCOMPARE(info.software, QStringLiteral("Edi??o ? 1.0"));

    // Curto (cabe na própria entrada).
    info = parseExif(exifForPublication({}, QStringLiteral("abc")));
    QCOMPARE(info.software, QStringLiteral("abc"));

    // Grande demais para o APP1: mínimo com as datas.
    const QByteArray big = readAll(data("exif/big_exif.bin"));
    QVERIFY(big.size() > kMaxApp1Payload - 1000);
    const ExifInfo bigInfo = parseExif(big);
    QVERIFY(bigInfo.valid);
    const QByteArray out = exifForPublication(big, kSoftware);
    QVERIFY(out.size() <= kMaxApp1Payload);
    QVERIFY(out.size() < 1000);
    info = parseExif(out);
    QCOMPARE(info.orientation, 1);
    QCOMPARE(info.software, kSoftware);
    QCOMPARE(info.capturedAt, bigInfo.capturedAt);
    QVERIFY(info.capturedAt.isValid());
    QCOMPARE(info.offsetTimeOriginal, QStringLiteral("-03:00"));
    dump(QStringLiteral("minimal_big.exif"), out);
}

void ExifTest::insertExifReplaces()
{
    const QByteArray jpeg = readAll(data("exif/mm_o6_thumb.jpg"));
    const QByteArray exif = exifForPublication({}, kSoftware);
    const QByteArray out = insertExif(jpeg, exif);
    QVERIFY(!out.isEmpty());
    QCOMPARE(markers(out).mid(0, 2), (QList<int>{0xE0, 0xE1}));
    QCOMPARE(markers(out).count(0xE1), 1);
    const auto header = parseJpegHeader(out);
    QVERIFY(header);
    QCOMPARE(header->exif, exif);
    QCOMPARE(header->width, 640);
    QVERIFY(!decodeRgb(out).empty());

    // Sem APP0: entra logo depois do SOI.
    QByteArray noApp0 = jpeg;
    const int app0Len = (uchar(jpeg[4]) << 8) | uchar(jpeg[5]);
    noApp0.remove(2, 2 + app0Len);
    QCOMPARE(markers(insertExif(noApp0, exif)).first(), 0xE1);

    QVERIFY(insertExif(QByteArray("nope"), exif).isEmpty());
    QVERIFY(insertExif(jpeg, QByteArray(kMaxApp1Payload + 1, 'x')).isEmpty());
}

void ExifTest::publishGeometry()
{
    QCOMPARE(targetBox(4000, 3000), cv::Size(1920, 1080));
    QCOMPARE(targetBox(3000, 4000), cv::Size(1080, 1920));
    QCOMPARE(targetBox(2000, 2000), cv::Size(1080, 1080));

    QCOMPARE(instagramSize(1.25), cv::Size(1080, 1350));
    QCOMPARE(instagramSize(1.0), cv::Size(1080, 1080));
    QCOMPARE(instagramSize(1.0 / 1.91), cv::Size(1080, 566));
    QCOMPARE(instagramSize(0.0), cv::Size(1080, 1));

    const cv::Mat land(3000, 4000, CV_8UC3, cv::Scalar(10, 20, 30));
    QCOMPARE(fit(land).size(), cv::Size(1440, 1080));
    const cv::Mat wide(1000, 4000, CV_8UC3, cv::Scalar(10, 20, 30));
    QCOMPARE(fit(wide).size(), cv::Size(1920, 480));
    const cv::Mat port(4000, 3000, CV_8UC3, cv::Scalar(10, 20, 30));
    QCOMPARE(fit(port).size(), cv::Size(1080, 1440));
    const cv::Mat square(2500, 2500, CV_8UC3, cv::Scalar(10, 20, 30));
    QCOMPARE(fit(square).size(), cv::Size(1080, 1080));
    const cv::Mat small(800, 1000, CV_8UC3, cv::Scalar(10, 20, 30));
    QCOMPARE(fit(small).data, small.data);  // nunca amplia

    QCOMPARE(fitExact(small, {1080, 864}).size(), cv::Size(1080, 864));
    QCOMPARE(fitExact(land, {1080, 810}).size(), cv::Size(1080, 810));
    QCOMPARE(fitExact(small, {1000, 800}).data, small.data);

    const QString tag = softwareTag();
    QVERIFY2(QRegularExpression(QStringLiteral("^\\S.* \\S+ \\(.+\\)$")).match(tag).hasMatch(), qPrintable(tag));
}

void ExifTest::publishLoadDraft()
{
    // 640x480 Orientation 6: pedido 100 -> 1/4 (160x120) -> girada 120x160.
    LoadedPhoto p = load(data("exif/mm_o6_thumb.jpg"), 1.0, 100);
    QCOMPARE(p.rgb.cols, 120);
    QCOMPARE(p.rgb.rows, 160);
    QVERIFY(p.exif.startsWith(QByteArray("Exif\0\0", 6)));
    QCOMPARE(parseExif(p.exif).orientation, 6);
    // Recorte de 50 %: pedido 200 -> 1/2.
    p = load(data("exif/mm_o6_thumb.jpg"), 0.5, 100);
    QCOMPARE(p.rgb.cols, 240);
    QCOMPARE(p.rgb.rows, 320);
    // Padrão (1920): resolução cheia.
    p = load(data("exif/mm_o6_thumb.jpg"));
    QCOMPARE(p.rgb.cols, 480);
    QCOMPARE(p.rgb.rows, 640);
    const auto c = corners(p.rgb);
    QCOMPARE((QList<int>{c[0], c[1], c[2], c[3]}), (QList<int>{Blue, Red, White, Green}));

    p = load(data("exif/no_exif.jpg"));
    QVERIFY(p.exif.isEmpty());
    QCOMPARE(p.rgb.cols, 64);
    QVERIFY(load(data("exif/missing.jpg")).rgb.empty());
}

void ExifTest::publishEncode()
{
    const QStringList files{QStringLiteral("sample.jpg"), QStringLiteral("exif/mm_o6_thumb.jpg"),
                            QStringLiteral("exif/ii_o8.jpg"), QStringLiteral("exif/no_exif.jpg")};
    for (const QString &file : files) {
        const LoadedPhoto p = load(data(file));
        QVERIFY(!p.rgb.empty());
        const cv::Mat rgb = fit(p.rgb);
        const QByteArray jpeg = encode(rgb, p.exif, kSoftware);
        QVERIFY(!jpeg.isEmpty());

        const QList<int> ms = markers(jpeg);
        QCOMPARE(ms.mid(0, 2), (QList<int>{0xE0, 0xE1}));  // APP0, depois o EXIF
        QVERIFY2(ms.contains(0xC2), "progressive (SOF2)");
        QVERIFY(!ms.contains(0xC0));
        // JFIF: unidade 1 (dpi), 72 x 72.
        QCOMPARE(jpeg.mid(6, 5), QByteArray("JFIF\0", 5));
        QCOMPARE(int(uchar(jpeg[13])), 1);
        QCOMPARE((uchar(jpeg[14]) << 8) | uchar(jpeg[15]), 72);
        QCOMPARE((uchar(jpeg[16]) << 8) | uchar(jpeg[17]), 72);

        const auto header = parseJpegHeader(jpeg);
        QVERIFY(header);
        QCOMPARE(header->width, rgb.cols);
        QCOMPARE(header->height, rgb.rows);
        const ExifInfo info = parseExif(header->exif);
        QCOMPARE(info.orientation, 1);
        QCOMPARE(info.software, kSoftware);
        QVERIFY(!info.hasIfd1);
        QCOMPARE(info.capturedAt, parseExif(p.exif).capturedAt);

        const cv::Mat back = decodeRgb(jpeg);
        QCOMPARE(back.size(), rgb.size());
        QVERIFY(cv::norm(back, rgb, cv::NORM_L1) / double(rgb.total() * 3) < 4.0);
        // Já sai em pé: reler aplica Orientation 1 (nada).
        const auto c0 = corners(rgb);
        const auto c1 = corners(back);
        QCOMPARE((QList<int>{c1[0], c1[1], c1[2], c1[3]}), (QList<int>{c0[0], c0[1], c0[2], c0[3]}));

        dump(QFileInfo(file).completeBaseName() + QStringLiteral("_publish.jpg"), jpeg);
    }

    // Qualidade do Instagram.
    const cv::Mat rgb = fitExact(load(data("exif/mm_o6_thumb.jpg")).rgb, instagramSize(1.25));
    QCOMPARE(rgb.size(), cv::Size(1080, 1350));
    const QByteArray q92 = encode(rgb, {}, kSoftware, kInstagramQuality);
    const QByteArray q50 = encode(rgb, {}, kSoftware, 50);
    QVERIFY(q92.size() > q50.size());
}

void ExifTest::derivatives()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString preview = tmp.filePath(QStringLiteral("prévia.jpg"));
    const QString thumb = tmp.filePath(QStringLiteral("miniatura.jpg"));
    QVERIFY(writePreview(data("exif/mm_o6_thumb.jpg"), preview));
    QVERIFY(writeThumbnail(preview, thumb));

    // O preview sai em pé e sem EXIF (Orientation ausente = 1).
    auto info = readInfo(preview);
    QVERIFY(info);
    QCOMPARE(info->width, 480);
    QCOMPARE(info->height, 640);
    QCOMPARE(info->orientation, 1);
    QVERIFY(readJpegHeader(preview)->exif.isEmpty());
    info = readInfo(thumb);
    QVERIFY(info);
    QCOMPARE(info->width, 180);
    QCOMPARE(info->height, 240);
    const cv::Mat t = loadRgb(thumb);
    const auto c = corners(t);
    QCOMPARE((QList<int>{c[0], c[1], c[2], c[3]}), (QList<int>{Blue, Red, White, Green}));
    // Baseline (não progressivo), como o _encode do Python.
    const QList<int> ms = markers(readAll(thumb));
    QVERIFY(ms.contains(0xC0));

    // writeAtomic substitui o arquivo inteiro e não deixa temporários.
    QVERIFY(writeAtomic(thumb, QByteArray("abc")));
    QCOMPARE(readAll(thumb), QByteArray("abc"));
    QCOMPARE(QDir(tmp.path()).entryList(QDir::Files).size(), 2);
    QVERIFY(!writeAtomic(tmp.filePath(QStringLiteral("nao/existe/x.jpg")), QByteArray("x")));
    QVERIFY(!writePreview(data("exif/missing.jpg"), tmp.filePath(QStringLiteral("p2.jpg"))));
}

QTEST_GUILESS_MAIN(ExifTest)
#include "tst_exif.moc"

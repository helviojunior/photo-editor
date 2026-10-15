// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Publish.h"

#include "imaging/Exif.h"
#include "imaging/ImageIO.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace pe::imaging {

namespace {

constexpr int kDensityUnitsOffset = 13;  ///< SOI(2) + marcador(2) + tamanho(2) + "JFIF\0"(5) + versão(2)

bool hasJfifApp0(const QByteArray &jpeg)
{
    return jpeg.size() >= 20 && uchar(jpeg[2]) == 0xFF && uchar(jpeg[3]) == 0xE0
           && jpeg.mid(6, 5) == QByteArray("JFIF\0", 5);
}

/// Grava ``dpi`` no APP0 JFIF (unidade 1 = pontos por polegada), como o
/// ``dpi=`` do Pillow. Sem APP0, insere um.
QByteArray withDpi(QByteArray jpeg, int dpi)
{
    const char hi = char((dpi >> 8) & 0xFF);
    const char lo = char(dpi & 0xFF);
    if (!hasJfifApp0(jpeg)) {
        const char app0[] = {'\xFF', '\xE0', 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 1, hi, lo, hi, lo, 0, 0};
        jpeg.insert(2, QByteArray(app0, sizeof(app0)));
        return jpeg;
    }
    jpeg[kDensityUnitsOffset] = 1;
    jpeg[kDensityUnitsOffset + 1] = hi;
    jpeg[kDensityUnitsOffset + 2] = lo;
    jpeg[kDensityUnitsOffset + 3] = hi;
    jpeg[kDensityUnitsOffset + 4] = lo;
    return jpeg;
}

/// ``round()`` do Python: metade vai para o par.
int pyRound(double x)
{
    return int(std::nearbyint(x));
}

} // namespace

cv::Size targetBox(int width, int height)
{
    if (height > width)
        return {kTargetShortSide, kTargetLongSide};
    if (height == width)
        return {kTargetShortSide, kTargetShortSide};
    return {kTargetLongSide, kTargetShortSide};
}

LoadedPhoto load(const QString &path, double cropScale, int longSide)
{
    LoadedPhoto out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray data = f.readAll();
    f.close();

    const int need = int(std::ceil(longSide / std::max(cropScale, 0.01)));
    const auto header = parseJpegHeader(data);
    if (header)
        out.exif = header->exif;
    // A DCT reduz por 1/2, 1/4, 1/8 mantendo os dois lados >= o pedido.
    const int scale = header ? draftScale(header->width, header->height, need) : 1;
    const cv::Mat img = decodeRgb(data, scale);
    if (img.empty())
        return out;
    out.rgb = applyOrientation(img, parseExif(out.exif).orientation);
    return out;
}

cv::Mat fit(const cv::Mat &rgb)
{
    const int w = rgb.cols;
    const int h = rgb.rows;
    cv::Size box = targetBox(w, h);
    if (w <= box.width && h <= box.height)
        return rgb;
    // ImageOps.contain: a dimensão que sobra segue a proporção da imagem.
    const double imRatio = double(w) / h;
    const double destRatio = double(box.width) / box.height;
    if (imRatio != destRatio) {
        if (imRatio > destRatio)
            box.height = pyRound(double(h) / w * box.width);
        else
            box.width = pyRound(double(w) / h * box.height);
    }
    return resizeTo(rgb, box);
}

cv::Size instagramSize(double aspect)
{
    return {kInstagramWidth, std::max(int(std::ceil(kInstagramWidth * aspect - 1e-6)), 1)};
}

cv::Mat fitExact(const cv::Mat &rgb, cv::Size size)
{
    return resizeTo(rgb, size);
}

QString softwareTag()
{
    return QStringLiteral("%1 %2 (%3)")
        .arg(QString::fromUtf8(PHOTOEDITOR_BRAND_NAME), QString::fromUtf8(PHOTOEDITOR_VERSION),
             QString::fromUtf8(PHOTOEDITOR_BRAND_URL));
}

QByteArray encode(const cv::Mat &rgb, const QByteArray &exif, const QString &software, int quality)
{
    QByteArray jpeg = encodeJpeg(rgb, quality, true);
    if (jpeg.isEmpty())
        return {};
    jpeg = withDpi(std::move(jpeg), kTargetDpi);
    const QByteArray clean = exifForPublication(exif, software);
    const QByteArray withExif = insertExif(jpeg, clean);
    return withExif.isEmpty() ? jpeg : withExif;
}

} // namespace pe::imaging

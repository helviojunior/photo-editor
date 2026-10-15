// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/ImageIO.h"

#include "imaging/Exif.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pe::imaging {

namespace {

const QByteArray kExifHeader("Exif\0\0", 6);

bool isSofMarker(uchar m)
{
    // SOF0..SOF15, menos DHT (C4), JPG (C8) e DAC (CC).
    return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
}

/// Percorre os segmentos até o SOS, lendo só o que importa (SOF e APP1 Exif);
/// o resto é pulado com seek. Funciona com arquivo ou buffer em memória.
std::optional<JpegHeader> parseHeader(QIODevice &dev)
{
    const QByteArray soi = dev.read(2);
    if (soi.size() != 2 || uchar(soi[0]) != 0xFF || uchar(soi[1]) != 0xD8)
        return std::nullopt;

    JpegHeader header;
    bool haveSof = false;
    bool haveExif = false;
    for (;;) {
        char c = 0;
        if (!dev.getChar(&c))
            break;
        if (uchar(c) != 0xFF)
            break;  // fora de sincronia: para por aqui
        uchar marker = 0xFF;
        while (marker == 0xFF && dev.getChar(&c))  // bytes de enchimento
            marker = uchar(c);
        if (marker == 0xFF)
            break;
        if (marker == 0xDA || marker == 0xD9)
            break;
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01)
            continue;  // marcadores sem tamanho
        const QByteArray lenBytes = dev.read(2);
        if (lenBytes.size() != 2)
            break;
        const int len = (uchar(lenBytes[0]) << 8) | uchar(lenBytes[1]);
        if (len < 2)
            break;
        const qint64 payloadLen = len - 2;
        if (isSofMarker(marker) && !haveSof) {
            const QByteArray p = dev.read(payloadLen);
            if (p.size() < 5)
                break;
            header.height = (uchar(p[1]) << 8) | uchar(p[2]);
            header.width = (uchar(p[3]) << 8) | uchar(p[4]);
            haveSof = header.width > 0 && header.height > 0;
            continue;
        }
        if (marker == 0xE1 && !haveExif) {
            const QByteArray p = dev.read(payloadLen);
            if (p.size() != payloadLen)
                break;
            if (p.startsWith(kExifHeader)) {
                header.exif = p;
                haveExif = true;
            }
            continue;
        }
        if (!dev.seek(dev.pos() + payloadLen))
            break;
    }
    if (!haveSof)
        return std::nullopt;
    return header;
}

/// ``round_aspect`` do Pillow: floor ou ceil, o que der a proporção mais
/// próxima (empate fica com o floor); nunca menos que 1.
template<typename Key>
int roundAspect(double number, Key key)
{
    const int lo = int(std::floor(number));
    const int hi = int(std::ceil(number));
    return std::max(key(hi) < key(lo) ? hi : lo, 1);
}

} // namespace

bool isJpeg(const QString &path)
{
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    return fi.isFile() && (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"));
}

std::optional<JpegHeader> readJpegHeader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return parseHeader(f);
}

std::optional<JpegHeader> parseJpegHeader(const QByteArray &data)
{
    QByteArray copy = data;  // QBuffer pede não-const; é só uma referência compartilhada
    QBuffer buf(&copy);
    if (!buf.open(QIODevice::ReadOnly))
        return std::nullopt;
    return parseHeader(buf);
}

std::optional<JpegInfo> readInfo(const QString &path)
{
    const auto header = readJpegHeader(path);
    if (!header)
        return std::nullopt;
    const ExifInfo exif = parseExif(header->exif);
    JpegInfo info;
    info.orientation = exif.orientation;
    info.width = header->width;
    info.height = header->height;
    if (info.orientation >= 5)  // giro de 90 graus troca os lados
        std::swap(info.width, info.height);
    info.capturedAt = exif.capturedAt;
    return info;
}

cv::Mat applyOrientation(const cv::Mat &img, int orientation)
{
    cv::Mat out;
    switch (orientation) {
    case 2: cv::flip(img, out, 1); return out;                          // FLIP_LEFT_RIGHT
    case 3: cv::rotate(img, out, cv::ROTATE_180); return out;           // ROTATE_180
    case 4: cv::flip(img, out, 0); return out;                          // FLIP_TOP_BOTTOM
    case 5: cv::transpose(img, out); return out;                        // TRANSPOSE
    case 6: cv::rotate(img, out, cv::ROTATE_90_CLOCKWISE); return out;  // ROTATE_270
    case 7: {                                                           // TRANSVERSE
        cv::Mat t;
        cv::transpose(img, t);
        cv::flip(t, out, -1);
        return out;
    }
    case 8: cv::rotate(img, out, cv::ROTATE_90_COUNTERCLOCKWISE); return out;  // ROTATE_90
    default: return img;
    }
}

int draftScale(int width, int height, int request)
{
    if (request <= 0 || width <= 0 || height <= 0)
        return 1;
    const int scale = std::min(width / request, height / request);
    for (int a : {8, 4, 2})
        if (scale >= a)
            return a;
    return 1;
}

cv::Size thumbnailSize(cv::Size size, int maxSide)
{
    int x = maxSide;
    int y = maxSide;
    if (maxSide <= 0 || (x >= size.width && y >= size.height))
        return size;
    const double aspect = double(size.width) / size.height;
    if (double(x) / y >= aspect) {
        x = roundAspect(y * aspect, [&](int n) { return std::abs(aspect - double(n) / y); });
    } else {
        y = roundAspect(x / aspect, [&](int n) { return n == 0 ? 0.0 : std::abs(aspect - double(x) / n); });
    }
    return {x, y};
}

cv::Mat resizeTo(const cv::Mat &img, cv::Size size)
{
    if (img.empty() || img.size() == size)
        return img;
    const bool shrink = size.width <= img.cols && size.height <= img.rows;
    cv::Mat out;
    cv::resize(img, out, size, 0, 0, shrink ? cv::INTER_AREA : cv::INTER_LANCZOS4);
    return out;
}

cv::Mat decodeRgb(const QByteArray &data, int scale)
{
    if (data.isEmpty())
        return {};
    int flags = cv::IMREAD_COLOR;
    switch (scale) {
    case 2: flags = cv::IMREAD_REDUCED_COLOR_2; break;
    case 4: flags = cv::IMREAD_REDUCED_COLOR_4; break;
    case 8: flags = cv::IMREAD_REDUCED_COLOR_8; break;
    default: break;
    }
    const cv::Mat buf(1, int(data.size()), CV_8UC1, const_cast<char *>(data.constData()));
    cv::Mat bgr;
    try {
        bgr = cv::imdecode(buf, flags | cv::IMREAD_IGNORE_ORIENTATION);
    } catch (const cv::Exception &) {
        return {};
    }
    if (bgr.empty() || bgr.type() != CV_8UC3)
        return {};
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return rgb;
}

cv::Mat loadRgb(const QString &path, int maxSide)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    f.close();

    const auto header = parseJpegHeader(data);
    const int orientation = header ? parseExif(header->exif).orientation : 1;
    const int scale = (header && maxSide > 0) ? draftScale(header->width, header->height, maxSide) : 1;
    cv::Mat img = decodeRgb(data, scale);
    if (img.empty())
        return {};
    img = applyOrientation(img, orientation);
    if (maxSide > 0)
        img = resizeTo(img, thumbnailSize(img.size(), maxSide));
    return img;
}

QByteArray encodeJpeg(const cv::Mat &rgb, int quality, bool progressive)
{
    if (rgb.empty() || rgb.type() != CV_8UC3)
        return {};
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    const std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, quality,
                                  cv::IMWRITE_JPEG_OPTIMIZE, 1,
                                  cv::IMWRITE_JPEG_PROGRESSIVE, progressive ? 1 : 0};
    std::vector<uchar> out;
    try {
        if (!cv::imencode(".jpg", bgr, out, params))
            return {};
    } catch (const cv::Exception &) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.data()), qsizetype(out.size()));
}

bool writeAtomic(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

bool writePreview(const QString &source, const QString &dest)
{
    const cv::Mat img = loadRgb(source, kPreviewSide);
    const QByteArray jpeg = encodeJpeg(img, kPreviewQuality);
    return !jpeg.isEmpty() && writeAtomic(dest, jpeg);
}

bool writeThumbnail(const QString &preview, const QString &dest)
{
    const cv::Mat img = loadRgb(preview, kThumbnailSide);
    const QByteArray jpeg = encodeJpeg(img, kThumbnailQuality);
    return !jpeg.isEmpty() && writeAtomic(dest, jpeg);
}

} // namespace pe::imaging

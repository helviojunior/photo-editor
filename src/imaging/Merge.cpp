// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Merge.h"

#include <QByteArray>

#include "imaging/SettingsHash.h"

#include <QJsonArray>

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>

namespace pe::imaging {

namespace {

/// round(x, n) do Python: arredondamento CORRETO do valor binário exato (o
/// meio, raro, vai para o par) — o mesmo que o printf "%.nf" faz.
double pyRound(double x, int digits)
{
    if (!std::isfinite(x))
        return x;
    char buf[512];
    const auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, digits);
    // Como o Python, um negativo pequeno vira -0.0 ("-0.00" -> -0.0). Leitura
    // pelo QByteArray: independe do locale e não exige o from_chars(double),
    // que o libc++ da Apple só tem a partir do macOS 26.
    bool ok = false;
    const double out = QByteArray(buf, static_cast<qsizetype>(res.ptr - buf)).toDouble(&ok);
    return ok ? out : x;
}

cv::Mat claheGray(const cv::Mat &rgb)
{
    cv::Mat g, out;
    cv::cvtColor(rgb, g, cv::COLOR_RGB2GRAY);
    // Equaliza o contraste local: uma foto um pouco mais escura acha os
    // mesmos pontos.
    cv::createCLAHE(2.0, cv::Size(8, 8))->apply(g, out);
    return out;
}

double median(std::vector<double> v)
{
    if (v.empty())
        return std::numeric_limits<double>::quiet_NaN();
    const size_t n = v.size();
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(n / 2);
    std::nth_element(v.begin(), mid, v.end());
    if (n % 2)
        return *mid;
    const double hi = *mid;
    const double lo = *std::max_element(v.begin(), mid);
    return (lo + hi) / 2.0;
}

/// ``rgb.astype(float32) * gain`` limitado a [0, 255].
cv::Mat applyGain(const cv::Mat &rgbU8, const cv::Vec3d &gain)
{
    const cv::Vec3f g(static_cast<float>(gain[0]), static_cast<float>(gain[1]), static_cast<float>(gain[2]));
    cv::Mat out(rgbU8.size(), CV_32FC3);
    for (int r = 0; r < rgbU8.rows; ++r) {
        const cv::Vec3b *p = rgbU8.ptr<cv::Vec3b>(r);
        cv::Vec3f *o = out.ptr<cv::Vec3f>(r);
        for (int c = 0; c < rgbU8.cols; ++c) {
            for (int k = 0; k < 3; ++k)
                o[c][k] = std::clamp(static_cast<float>(p[c][k]) * g[k], 0.f, 255.f);
        }
    }
    return out;
}

/// ``trunc(clip(v, 0, 255))`` do ``astype(np.uint8)``.
uchar truncByte(float v)
{
    return v <= 0.f ? 0 : (v >= 255.f ? 255 : static_cast<uchar>(v));
}

QJsonArray matrixJson(const cv::Matx23d &m)
{
    return QJsonArray{QJsonArray{m(0, 0), m(0, 1), m(0, 2)}, QJsonArray{m(1, 0), m(1, 1), m(1, 2)}};
}

} // namespace

// --------------------------------------------------------------------------- //
// JSON
// --------------------------------------------------------------------------- //

QJsonObject mergeAlignToJson(const MergeAlign &a)
{
    return QJsonObject{
        {QStringLiteral("matrix"), matrixJson(a.matrix)},
        {QStringLiteral("inliers"), a.inliers},
        {QStringLiteral("error"), a.error ? QJsonValue(*a.error) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("ok"), a.ok},
        {QStringLiteral("gain"), QJsonArray{a.gain[0], a.gain[1], a.gain[2]}},
    };
}

MergeAlign mergeAlignFromJson(const QJsonObject &obj)
{
    MergeAlign a;
    const QJsonArray rows = obj.value(QStringLiteral("matrix")).toArray();
    if (rows.size() == 2 && rows.at(0).toArray().size() == 3 && rows.at(1).toArray().size() == 3) {
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 3; ++c)
                a.matrix(r, c) = rows.at(r).toArray().at(c).toDouble();
        }
    }
    a.inliers = obj.value(QStringLiteral("inliers")).toInt();
    const QJsonValue err = obj.value(QStringLiteral("error"));
    if (err.isDouble())
        a.error = err.toDouble();
    a.ok = obj.value(QStringLiteral("ok")).toBool();
    const QJsonArray gain = obj.value(QStringLiteral("gain")).toArray();
    if (gain.size() == 3) {
        for (int c = 0; c < 3; ++c)
            a.gain[c] = gain.at(c).toDouble(1.0);
    }
    return a;
}

// --------------------------------------------------------------------------- //
// imaging/merge.py
// --------------------------------------------------------------------------- //

MergeAlign align(const cv::Mat &baseRgb, const cv::Mat &srcRgb, const cv::Mat &exclude)
{
    CV_Assert(baseRgb.type() == CV_8UC3 && srcRgb.type() == CV_8UC3);
    const cv::Ptr<cv::SIFT> sift = cv::SIFT::create(kMergeFeatures);
    std::vector<cv::KeyPoint> kb, ks;
    cv::Mat db, ds;
    sift->detectAndCompute(claheGray(baseRgb), cv::noArray(), kb, db);
    cv::Mat keep;
    if (!exclude.empty()) {
        CV_Assert(exclude.type() == CV_8UC1 && exclude.size() == srcRgb.size());
        // Com folga: a borda do objeto também se mexeu.
        const int grow = std::max(srcRgb.rows, srcRgb.cols) / 80 * 2 + 1;
        cv::Mat grown;
        cv::dilate(exclude > 0, grown, cv::Mat::ones(grow, grow, CV_8U));
        keep = grown == 0;
    }
    // Máscara vazia = a imagem inteira.
    sift->detectAndCompute(claheGray(srcRgb), keep, ks, ds);

    MergeAlign fail;
    fail.matrix = cv::Matx23d(static_cast<double>(baseRgb.cols) / srcRgb.cols, 0, 0, 0,
                              static_cast<double>(baseRgb.rows) / srcRgb.rows, 0);
    if (db.empty() || ds.empty() || static_cast<int>(kb.size()) < kMergeMinInliers
        || static_cast<int>(ks.size()) < kMergeMinInliers)
        return fail;

    std::vector<std::vector<cv::DMatch>> pairs;
    cv::BFMatcher(cv::NORM_L2).knnMatch(ds, db, pairs, 2);
    std::vector<cv::Point2f> src, dst;
    for (const auto &p : pairs) {
        if (p.size() == 2 && p[0].distance < kMergeLoweRatio * p[1].distance) {
            src.push_back(ks[p[0].queryIdx].pt);
            dst.push_back(kb[p[0].trainIdx].pt);
        }
    }
    if (static_cast<int>(src.size()) < kMergeMinInliers)
        return fail;

    cv::Mat inlierMask;
    cv::Mat matrix = cv::estimateAffinePartial2D(src, dst, inlierMask, cv::RANSAC, kMergeRansacPx, 5000, 0.999);
    if (matrix.empty())
        return fail;
    std::vector<cv::Point2f> srcIn, dstIn;
    for (size_t i = 0; i < src.size(); ++i) {
        if (inlierMask.at<uchar>(static_cast<int>(i))) {
            srcIn.push_back(src[i]);
            dstIn.push_back(dst[i]);
        }
    }
    if (static_cast<int>(srcIn.size()) < kMergeMinInliers)
        return fail;
    // Refina só com o fundo: o RANSAC escolhe, o mínimo quadrado ajusta.
    const cv::Mat refined = cv::estimateAffinePartial2D(srcIn, dstIn, cv::noArray(), cv::LMEDS);
    if (!refined.empty())
        matrix = refined;

    MergeAlign out;
    matrix.convertTo(matrix, CV_64F);
    out.matrix = cv::Matx23d(matrix.ptr<double>());
    const cv::Matx23d &m = out.matrix;
    std::vector<double> err;
    err.reserve(srcIn.size());
    for (size_t i = 0; i < srcIn.size(); ++i) {
        const double x = srcIn[i].x, y = srcIn[i].y;
        const double ex = x * m(0, 0) + y * m(0, 1) + m(0, 2) - dstIn[i].x;
        const double ey = x * m(1, 0) + y * m(1, 1) + m(1, 2) - dstIn[i].y;
        err.push_back(std::hypot(ex, ey));
    }
    out.inliers = static_cast<int>(srcIn.size());
    out.error = pyRound(median(err), 3);
    out.ok = true;
    out.gain = alignGain(baseRgb, srcRgb, out.matrix);
    return out;
}

cv::Vec3d alignGain(const cv::Mat &baseRgb, const cv::Mat &srcRgb, const cv::Matx23d &matrix)
{
    const cv::Size size = baseRgb.size();
    cv::Mat warped, valid;
    cv::warpAffine(srcRgb, warped, cv::Mat(matrix), size, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    cv::warpAffine(cv::Mat(srcRgb.size(), CV_8U, cv::Scalar(255)), valid, cv::Mat(matrix), size, cv::INTER_NEAREST);

    cv::Vec3d sumB(0, 0, 0), sumS(0, 0, 0);
    long count = 0;
    for (int r = 0; r < size.height; ++r) {
        const cv::Vec3b *b = baseRgb.ptr<cv::Vec3b>(r);
        const cv::Vec3b *s = warped.ptr<cv::Vec3b>(r);
        const uchar *v = valid.ptr<uchar>(r);
        for (int c = 0; c < size.width; ++c) {
            if (!v[c])
                continue;
            float diff = 0.f;
            for (int k = 0; k < 3; ++k)
                diff = std::max(diff, std::abs(static_cast<float>(b[c][k]) - static_cast<float>(s[c][k])));
            if (!(diff < kMergeStaticDiff))
                continue;
            // Nem estourado nem preto: lá o sensor não mede a luz.
            const float lum = (static_cast<float>(b[c][0]) + static_cast<float>(b[c][1]) + static_cast<float>(b[c][2])) / 3.f;
            if (!(lum > 10.f && lum < 245.f))
                continue;
            for (int k = 0; k < 3; ++k) {
                sumB[k] += b[c][k];
                sumS[k] += s[c][k];
            }
            ++count;
        }
    }
    if (count < 1000)
        return {1.0, 1.0, 1.0};
    cv::Vec3d out;
    for (int k = 0; k < 3; ++k) {
        const double ratio = (sumB[k] / count) / std::max(sumS[k] / count, 1.0);
        out[k] = pyRound(std::min(std::max(ratio, kMergeGainMin), kMergeGainMax), 4);
    }
    return out;
}

MergeDescription describe(const cv::Matx23d &m)
{
    MergeDescription d;
    d.scale = pyRound(std::hypot(m(0, 0), m(1, 0)), 5);
    d.angle = pyRound(std::atan2(m(1, 0), m(0, 0)) * 180.0 / std::numbers::pi, 4);
    d.dx = pyRound(m(0, 2), 2);
    d.dy = pyRound(m(1, 2), 2);
    return d;
}

QJsonObject describeToJson(const cv::Matx23d &matrix)
{
    const MergeDescription d = describe(matrix);
    return QJsonObject{{QStringLiteral("scale"), d.scale},
                       {QStringLiteral("angle"), d.angle},
                       {QStringLiteral("dx"), d.dx},
                       {QStringLiteral("dy"), d.dy}};
}

cv::Matx23d scaledMatrix(const cv::Matx23d &matrix, cv::Size srcFrom, cv::Size srcTo, cv::Size dstFrom,
                         cv::Size dstTo)
{
    const cv::Matx33d m(matrix(0, 0), matrix(0, 1), matrix(0, 2), matrix(1, 0), matrix(1, 1), matrix(1, 2), 0, 0, 1);
    const cv::Matx33d sIn = cv::Matx33d::diag({static_cast<double>(srcFrom.width) / srcTo.width,
                                               static_cast<double>(srcFrom.height) / srcTo.height, 1.0});
    const cv::Matx33d sOut = cv::Matx33d::diag({static_cast<double>(dstTo.width) / dstFrom.width,
                                                static_cast<double>(dstTo.height) / dstFrom.height, 1.0});
    const cv::Matx33d r = sOut * m * sIn;
    return {r(0, 0), r(0, 1), r(0, 2), r(1, 0), r(1, 1), r(1, 2)};
}

std::optional<MergePiece> cutout(const cv::Mat &srcRgb, const cv::Mat &alpha, const cv::Matx23d &matrix,
                                 const cv::Vec3d &gain, cv::Size outSize, cv::Point offset)
{
    CV_Assert(srcRgb.type() == CV_8UC3 && alpha.type() == CV_32FC1 && alpha.size() == srcRgb.size());
    const cv::Rect area = cv::boundingRect(alpha > 1e-3f);
    if (area.empty())
        return std::nullopt;
    cv::Matx23d m = matrix;
    // Coordenadas do recorte -> da foto inteira.
    m(0, 2) += m(0, 0) * offset.x + m(0, 1) * offset.y;
    m(1, 2) += m(1, 0) * offset.x + m(1, 1) * offset.y;
    const double cx[4] = {static_cast<double>(area.x), static_cast<double>(area.x + area.width),
                          static_cast<double>(area.x), static_cast<double>(area.x + area.width)};
    const double cy[4] = {static_cast<double>(area.y), static_cast<double>(area.y),
                          static_cast<double>(area.y + area.height), static_cast<double>(area.y + area.height)};
    double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
    for (int i = 0; i < 4; ++i) {
        const double x = cx[i] * m(0, 0) + cy[i] * m(0, 1) + m(0, 2);
        const double y = cx[i] * m(1, 0) + cy[i] * m(1, 1) + m(1, 2);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    const int x0 = std::max(static_cast<int>(std::floor(minX)) - 2, 0);
    const int y0 = std::max(static_cast<int>(std::floor(minY)) - 2, 0);
    const int x1 = std::min(static_cast<int>(std::ceil(maxX)) + 2, outSize.width);
    const int y1 = std::min(static_cast<int>(std::ceil(maxY)) + 2, outSize.height);
    if (x1 <= x0 || y1 <= y0)
        return std::nullopt;
    // Desloca a origem para o canto da caixa.
    cv::Matx23d local = m;
    local(0, 2) -= x0;
    local(1, 2) -= y0;
    const cv::Size size(x1 - x0, y1 - y0);
    cv::Mat rgb, a;
    cv::warpAffine(srcRgb, rgb, cv::Mat(local), size, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    cv::warpAffine(alpha, a, cv::Mat(local), size, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    MergePiece piece;
    piece.rgb = applyGain(rgb, gain);
    cv::min(cv::max(a, 0.0), 1.0, piece.alpha);
    piece.origin = {x0, y0};
    return piece;
}

MergePiece backdrop(const cv::Mat &srcRgb, const cv::Mat &alpha, cv::Point origin, const cv::Matx23d &matrix,
                    const cv::Vec3d &gain)
{
    CV_Assert(srcRgb.type() == CV_8UC3 && alpha.type() == CV_32FC1);
    cv::Matx23d m = matrix;
    m(0, 2) -= origin.x;
    m(1, 2) -= origin.y;
    cv::Mat rgb;
    cv::warpAffine(srcRgb, rgb, cv::Mat(m), alpha.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return {applyGain(rgb, gain), alpha, origin};
}

void paste(cv::Mat &canvas, const MergePiece &piece, double opacity)
{
    CV_Assert(canvas.type() == CV_8UC3 && piece.rgb.type() == CV_32FC3 && piece.alpha.type() == CV_32FC1);
    const int h = piece.alpha.rows, w = piece.alpha.cols;
    CV_Assert(cv::Rect(0, 0, canvas.cols, canvas.rows).contains(piece.origin)
              && piece.origin.x + w <= canvas.cols && piece.origin.y + h <= canvas.rows);
    const float op = static_cast<float>(opacity);
    for (int r = 0; r < h; ++r) {
        cv::Vec3b *dst = canvas.ptr<cv::Vec3b>(piece.origin.y + r) + piece.origin.x;
        const cv::Vec3f *src = piece.rgb.ptr<cv::Vec3f>(r);
        const float *al = piece.alpha.ptr<float>(r);
        for (int c = 0; c < w; ++c) {
            const float a = al[c] * op;
            for (int k = 0; k < 3; ++k) {
                const float mixed = static_cast<float>(dst[c][k]) * (1.f - a) + src[c][k] * a;
                dst[c][k] = truncByte(mixed + 0.5f);
            }
        }
    }
}

// --------------------------------------------------------------------------- //
// services/merges.py (partes puras)
// --------------------------------------------------------------------------- //

std::vector<double> defaultOpacities(int n)
{
    if (n <= 1)
        return std::vector<double>(std::max(n, 0), kMergeLastOpacity);
    const double step = (kMergeLastOpacity - kMergeFirstOpacity) / (n - 1);
    std::vector<double> out;
    for (int i = 0; i < n; ++i)
        out.push_back(pyRound(kMergeFirstOpacity + i * step, 2));
    return out;
}

std::optional<std::pair<cv::Mat, cv::Point>> maskRegion(const cv::Mat &maskU8, cv::Size size)
{
    CV_Assert(maskU8.type() == CV_8UC1);
    const cv::Rect area = cv::boundingRect(maskU8 > 2);
    if (area.empty())
        return std::nullopt;
    const int fw = size.width, fh = size.height;
    const double sx = static_cast<double>(fw) / maskU8.cols;
    const double sy = static_cast<double>(fh) / maskU8.rows;
    const int margin = sx > 1.5 ? kMergeCropMargin : 2;
    const int x0 = std::max(static_cast<int>(area.x * sx) - margin, 0);
    const int y0 = std::max(static_cast<int>(area.y * sy) - margin, 0);
    const int x1 = std::min(static_cast<int>((area.x + area.width) * sx) + margin, fw);
    const int y1 = std::min(static_cast<int>((area.y + area.height) * sy) + margin, fh);
    // O centro do pixel x da caixa cai em (x0 + x + 0,5) / sx - 0,5 na
    // máscara (idem em y).
    const cv::Matx23d toMask(1 / sx, 0, (x0 + 0.5) / sx - 0.5, 0, 1 / sy, (y0 + 0.5) / sy - 0.5);
    cv::Mat m32(maskU8.size(), CV_32F);
    for (int r = 0; r < maskU8.rows; ++r) {
        const uchar *p = maskU8.ptr<uchar>(r);
        float *o = m32.ptr<float>(r);
        for (int c = 0; c < maskU8.cols; ++c)
            o[c] = static_cast<float>(p[c]) / 255.f;
    }
    cv::Mat alpha;
    cv::warpAffine(m32, alpha, cv::Mat(toMask), cv::Size(x1 - x0, y1 - y0), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                   cv::BORDER_CONSTANT, cv::Scalar::all(0));
    return std::make_pair(alpha, cv::Point(x0, y0));
}

QString alignVersion(const std::optional<cv::Matx23d> &matrix, const std::optional<cv::Vec3d> &gain)
{
    QString raw = QStringLiteral("[");
    if (matrix) {
        const cv::Matx23d &m = *matrix;
        raw += QStringLiteral("[[") + pyFloat(m(0, 0)) + QLatin1Char(',') + pyFloat(m(0, 1)) + QLatin1Char(',')
            + pyFloat(m(0, 2)) + QStringLiteral("],[") + pyFloat(m(1, 0)) + QLatin1Char(',') + pyFloat(m(1, 1))
            + QLatin1Char(',') + pyFloat(m(1, 2)) + QStringLiteral("]]");
    } else {
        raw += QStringLiteral("null");
    }
    raw += QLatin1Char(',');
    if (gain) {
        raw += QLatin1Char('[') + pyFloat((*gain)[0]) + QLatin1Char(',') + pyFloat((*gain)[1]) + QLatin1Char(',')
            + pyFloat((*gain)[2]) + QLatin1Char(']');
    } else {
        raw += QStringLiteral("null");
    }
    raw += QLatin1Char(']');
    return sha1Hex(raw.toUtf8()).left(10);
}

QString alignVersion(const QJsonObject &align)
{
    std::optional<cv::Matx23d> matrix;
    std::optional<cv::Vec3d> gain;
    if (align.contains(QStringLiteral("matrix")) && !align.value(QStringLiteral("matrix")).isNull())
        matrix = mergeAlignFromJson(align).matrix;
    if (align.contains(QStringLiteral("gain")) && !align.value(QStringLiteral("gain")).isNull())
        gain = mergeAlignFromJson(align).gain;
    return alignVersion(matrix, gain);
}

QString mergeExportName(const QString &fileName)
{
    // Path(file_name).stem + sufixo + Path(file_name).suffix
    QString name = fileName.mid(fileName.lastIndexOf(QLatin1Char('/')) + 1);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    QString stem = name, suffix;
    if (dot > 0 && dot < name.size() - 1) {
        stem = name.left(dot);
        suffix = name.mid(dot);
    }
    return stem + QLatin1String(kMergeExportSuffix) + suffix;
}

std::optional<cv::Vec4i> maskBounds(const cv::Mat &maskU8, int threshold)
{
    CV_Assert(maskU8.type() == CV_8UC1);
    const cv::Rect r = cv::boundingRect(maskU8 > threshold);
    if (r.empty())
        return std::nullopt;
    return cv::Vec4i(r.x, r.y, r.x + r.width - 1, r.y + r.height - 1);
}

bool layerClearsBaseArea(const cv::Mat &baseMask, const cv::Mat &layerMask, const cv::Matx23d &m)
{
    const auto base = maskBounds(baseMask);
    const auto layer = maskBounds(layerMask);
    if (!base || !layer)
        return false;
    const int margin = kMergeCropMargin;
    const double box[4] = {static_cast<double>((*base)[0] - margin), static_cast<double>((*base)[1] - margin),
                           static_cast<double>((*base)[2] + margin), static_cast<double>((*base)[3] + margin)};
    const auto &l = *layer;
    const double cx[4] = {static_cast<double>(l[0]), static_cast<double>(l[2]), static_cast<double>(l[0]),
                          static_cast<double>(l[2])};
    const double cy[4] = {static_cast<double>(l[1]), static_cast<double>(l[3]), static_cast<double>(l[3]),
                          static_cast<double>(l[1])};
    double minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
    for (int i = 0; i < 4; ++i) {
        const double x = cx[i] * m(0, 0) + cy[i] * m(0, 1) + m(0, 2);
        const double y = cx[i] * m(1, 0) + cy[i] * m(1, 1) + m(1, 2);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    return maxX < box[0] || minX > box[2] || maxY < box[1] || minY > box[3];
}

cv::Mat pieceToRgba(const std::optional<MergePiece> &piece, cv::Size outSize)
{
    cv::Mat rgba = cv::Mat::zeros(outSize, CV_8UC4);
    if (!piece)
        return rgba;
    const int h = piece->alpha.rows, w = piece->alpha.cols;
    const cv::Point o = piece->origin;
    for (int r = 0; r < h; ++r) {
        if (o.y + r < 0 || o.y + r >= outSize.height)
            continue;
        cv::Vec4b *dst = rgba.ptr<cv::Vec4b>(o.y + r);
        const cv::Vec3f *src = piece->rgb.ptr<cv::Vec3f>(r);
        const float *al = piece->alpha.ptr<float>(r);
        for (int c = 0; c < w; ++c) {
            const int x = o.x + c;
            if (x < 0 || x >= outSize.width)
                continue;
            const uchar a = truncByte(al[c] * 255.f + 0.5f);
            // Fora da área o RGB não aparece; zerado, o PNG fica pequeno.
            if (a == 0)
                continue;
            dst[x] = cv::Vec4b(truncByte(src[c][0] + 0.5f), truncByte(src[c][1] + 0.5f), truncByte(src[c][2] + 0.5f), a);
        }
    }
    return rgba;
}

} // namespace pe::imaging

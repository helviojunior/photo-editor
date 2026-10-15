// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Crop.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pe::imaging {

namespace {

double radians(double deg)
{
    return deg * std::numbers::pi / 180.0;
}

/// round(x, n) do Python (meio para o par no último dígito).
double roundTo(double x, int digits)
{
    const double f = std::pow(10.0, digits);
    return std::nearbyint(x * f) / f;
}

/// Meia caixa envolvente do quadro, em fração da largura e da altura.
std::pair<double, double> cropExtents(const Crop &crop, double aspect)
{
    const double c = std::abs(std::cos(radians(crop.angle)));
    const double s = std::abs(std::sin(radians(crop.angle)));
    const auto [bw, bh] = cropFrame(crop.ratio, aspect);
    return {crop.scale / 2 * (bw * c + bh * aspect * s), crop.scale / 2 * (bw * s / aspect + bh * c)};
}

} // namespace

const std::vector<InstagramRatio> &instagramRatios()
{
    static const std::vector<InstagramRatio> kRatios{
        {"4:5", 1.25},
        {"1:1", 1.0},
        {"1.91:1", 0.5236},  // round(1 / 1.91, 4)
    };
    return kRatios;
}

std::vector<double> instagramRatioValues()
{
    std::vector<double> out;
    for (const auto &r : instagramRatios())
        out.push_back(r.ratio);
    return out;
}

double instagramMaxAngle(double ratio)
{
    return ratio == 0.5236 ? kCropMaxAngle : kInstagramMaxAngle;
}

std::pair<double, double> instagramFeedAspect()
{
    return {0.5236 - 1e-3, 1.25 + 1e-3};
}

bool instagramFeedOk(const Crop &crop, double aspect)
{
    const auto [lo, hi] = instagramFeedAspect();
    const double out = cropOutputAspect(crop, aspect);
    return lo <= out && out <= hi;
}

std::pair<double, double> cropFrame(double ratio, double aspect)
{
    if (!(ratio > 0))
        return {1.0, 1.0};
    return {std::min(1.0, aspect / ratio), std::min(1.0, ratio / aspect)};
}

double cropMaxScale(double angle, double aspect, double ratio)
{
    const double c = std::abs(std::cos(radians(angle)));
    const double s = std::abs(std::sin(radians(angle)));
    const auto [bw, bh] = cropFrame(ratio, aspect);
    return std::min({1.0, 1.0 / (bw * c + bh * aspect * s), 1.0 / (bw * s / aspect + bh * c)});
}

double closestRatio(double target, const std::vector<double> &ratios)
{
    target = target > 0 ? target : 1.0;
    double best = ratios.front();
    double bestDist = std::abs(std::log(best / target));
    for (double r : ratios) {
        const double d = std::abs(std::log(r / target));
        if (d < bestDist) {
            best = r;
            bestDist = d;
        }
    }
    return best;
}

Crop normalizeCrop(const Crop &in, double aspect, const std::vector<double> &ratios)
{
    auto num = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
    aspect = aspect > 0 ? aspect : 1.0;
    double ratio = 0.0, maxAngle = kCropMaxAngle;
    if (!ratios.empty()) {
        const double asked = num(in.ratio, 0.0);
        ratio = roundTo(closestRatio(asked != 0.0 ? asked : aspect, ratios), 4);
        maxAngle = instagramMaxAngle(ratio);
    }
    Crop out;
    out.angle = roundTo(std::clamp(num(in.angle, 0.0), -maxAngle, maxAngle), 1);
    out.scale = std::min(std::max(num(in.scale, 1.0), kCropMinScale), cropMaxScale(out.angle, aspect, ratio));
    out.ratio = ratio;
    const auto [ex, ey] = cropExtents(out, aspect);
    out.cx = std::min(std::max(num(in.cx, 0.5), ex), 1.0 - ex);
    out.cy = std::min(std::max(num(in.cy, 0.5), ey), 1.0 - ey);
    out.scale = roundTo(out.scale, 4);
    out.cx = roundTo(out.cx, 4);
    out.cy = roundTo(out.cy, 4);
    out.ratio = roundTo(out.ratio, 4);
    return isCropIdentity(out) ? kCropIdentity : out;
}

Crop cropFromJson(const QJsonObject &obj)
{
    auto get = [&](const char *key, double fallback) {
        const QJsonValue v = obj.value(QLatin1String(key));
        if (v.isDouble())
            return v.toDouble();
        if (v.isString()) {
            bool ok = false;
            const double d = v.toString().toDouble(&ok);
            return ok ? d : fallback;
        }
        return fallback;
    };
    return {get("scale", 1.0), get("cx", 0.5), get("cy", 0.5), get("angle", 0.0), get("ratio", 0.0)};
}

QJsonObject cropToJson(const Crop &crop)
{
    return {{QStringLiteral("scale"), crop.scale}, {QStringLiteral("cx"), crop.cx},
            {QStringLiteral("cy"), crop.cy},       {QStringLiteral("angle"), crop.angle},
            {QStringLiteral("ratio"), crop.ratio}};
}

bool isCropIdentity(const Crop &crop)
{
    return crop.angle == 0 && crop.scale >= 0.9999 && crop.ratio == 0.0 && std::abs(crop.cx - 0.5) < 1e-4
        && std::abs(crop.cy - 0.5) < 1e-4;
}

double cropOutputAspect(const Crop &crop, double aspect)
{
    const auto [bw, bh] = cropFrame(crop.ratio, aspect);
    const double out = bh * aspect / bw;
    const auto [quarters, rest] = splitAngle(crop.angle);
    return (quarters % 2) ? 1.0 / out : out;
}

std::pair<int, double> splitAngle(double angle)
{
    const int quarters = static_cast<int>(std::floor((angle + 45.0) / 90.0));
    return {quarters, angle - 90.0 * quarters};
}

cv::Mat applyCrop(const cv::Mat &img, const Crop &crop, int interpolation)
{
    if (isCropIdentity(crop))
        return img;
    const int h = img.rows, w = img.cols;
    const auto [quarters, rest] = splitAngle(crop.angle);
    const auto [bw, bh] = cropFrame(crop.ratio, static_cast<double>(h) / w);
    double fw = crop.scale * bw * w, fh = crop.scale * bh * h;  // quadro, nos eixos dele
    if (quarters % 2)                                           // deitado: troca os lados
        std::swap(fw, fh);
    // round() do Python: meio para o par.
    const int ow = std::max(static_cast<int>(std::nearbyint(fw)), 1);
    const int oh = std::max(static_cast<int>(std::nearbyint(fh)), 1);
    const double t = radians(rest);
    const double cs = std::cos(t), sn = std::sin(t);
    const double cx = crop.cx * w, cy = crop.cy * h, ocx = ow / 2.0, ocy = oh / 2.0;
    // Saída -> origem: centro do quadro + rotação do deslocamento. Com
    // WARP_INVERSE_MAP o OpenCV usa a matriz nesse sentido, sem inverter.
    const cv::Matx23f m(static_cast<float>(cs), static_cast<float>(-sn), static_cast<float>(cx - cs * ocx + sn * ocy),
                        static_cast<float>(sn), static_cast<float>(cs), static_cast<float>(cy - sn * ocx - cs * ocy));
    cv::Mat out;
    cv::warpAffine(img, out, m, cv::Size(ow, oh), interpolation | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
    return out;
}

} // namespace pe::imaging

// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Look.h"

#include "imaging/SettingsHash.h"

#include <QCryptographicHash>

#include <opencv2/core/utility.hpp>

#include <algorithm>
#include <cmath>

namespace pe::imaging {

namespace {

/// 16 entradas por nível de 8 bits: k/255 cai exatamente na entrada 16k.
constexpr int kLutSize = 16 * 255 + 1;
/// Células do grão ao longo do lado maior: o grão acompanha o tamanho da imagem.
constexpr int kGrainCells = 1000;
constexpr double kGammaMin = 0.10;
constexpr double kGammaMax = 9.99;

const std::array<const char *, ChannelCount> kChannelNames{"rgb", "r", "g", "b"};

float clip01(float v)
{
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

int clampInt(double v, int lo, int hi, int fallback)
{
    if (!std::isfinite(v))
        return fallback;
    return std::clamp(static_cast<int>(std::lround(v)), lo, hi);
}

double round4(double v)
{
    const double r = std::nearbyint(v * 10000.0) / 10000.0;
    return r == 0.0 ? 0.0 : r;  // sem -0.0
}

QJsonValue jsonNumber(const QJsonObject &obj, const char *key)
{
    const QJsonValue v = obj.value(QLatin1String(key));
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        return ok ? QJsonValue(d) : QJsonValue();
    }
    return v.isDouble() ? v : QJsonValue();
}

/// Cúbica de Hermite com as tangentes de Fritsch–Carlson: passa pelos pontos,
/// não cria oscilação, e é monótona onde os pontos são.
struct Spline
{
    Curve p;
    std::vector<double> m;

    explicit Spline(const Curve &curve)
        : p(curve.size() < 2 ? Curve{{0.0, 0.0}, {1.0, 1.0}} : curve)
    {
        const size_t n = p.size();
        std::vector<double> d(n - 1);
        for (size_t k = 0; k + 1 < n; ++k)
            d[k] = (p[k + 1].y - p[k].y) / (p[k + 1].x - p[k].x);
        m.assign(n, 0.0);
        m[0] = d[0];
        m[n - 1] = d[n - 2];
        for (size_t k = 1; k + 1 < n; ++k)
            m[k] = (d[k - 1] * d[k] <= 0.0) ? 0.0 : (d[k - 1] + d[k]) / 2.0;
        for (size_t k = 0; k + 1 < n; ++k) {
            if (d[k] == 0.0) {
                m[k] = 0.0;
                m[k + 1] = 0.0;
                continue;
            }
            const double a = m[k] / d[k];
            const double b = m[k + 1] / d[k];
            const double s = a * a + b * b;
            if (s > 9.0) {
                const double t = 3.0 / std::sqrt(s);
                m[k] = t * a * d[k];
                m[k + 1] = t * b * d[k];
            }
        }
    }

    double operator()(double x) const
    {
        if (x <= p.front().x)
            return p.front().y;
        if (x >= p.back().x)
            return p.back().y;
        size_t k = 0;
        while (k + 2 < p.size() && x >= p[k + 1].x)
            ++k;
        const double h = p[k + 1].x - p[k].x;
        const double t = (x - p[k].x) / h;
        const double t2 = t * t, t3 = t2 * t;
        const double y = (2 * t3 - 3 * t2 + 1) * p[k].y + (t3 - 2 * t2 + t) * h * m[k]
            + (-2 * t3 + 3 * t2) * p[k + 1].y + (t3 - t2) * h * m[k + 1];
        return std::clamp(y, 0.0, 1.0);
    }
};

quint64 splitMix(quint64 x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/// Ruído ~N(0, 1) por célula (soma de 4 uniformes): só inteiros até o fim,
/// igual em toda plataforma.
float cellNoise(quint64 seed, int i, int j)
{
    quint64 h = splitMix(seed ^ splitMix((static_cast<quint64>(static_cast<quint32>(i)) << 32)
                                         | static_cast<quint32>(j)));
    double sum = 0.0;
    for (int k = 0; k < 4; ++k) {
        sum += static_cast<double>(h & 0xFFFF) / 65535.0;
        h >>= 16;
    }
    return static_cast<float>((sum - 2.0) * std::sqrt(3.0));
}

float luma709(const cv::Vec3f &px)
{
    return 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
}

void applyLuts(cv::Mat &x, const Look &look)
{
    std::array<std::vector<float>, 3> lut;
    std::array<std::optional<Spline>, ChannelCount> splines;
    for (int c = 0; c < ChannelCount; ++c) {
        if (!look.curves[c].empty())
            splines[c].emplace(look.curves[c]);
    }
    for (int c = 0; c < 3; ++c) {
        const Channel ch = static_cast<Channel>(c + 1);
        lut[c].resize(kLutSize);
        for (int i = 0; i < kLutSize; ++i) {
            double v = static_cast<double>(i) / (kLutSize - 1);
            if (look.levels[ChannelRgb] != kLevelsIdentity)
                v = applyLevels(look.levels[ChannelRgb], v);
            if (look.levels[ch] != kLevelsIdentity)
                v = applyLevels(look.levels[ch], v);
            if (splines[ChannelRgb])
                v = (*splines[ChannelRgb])(v);
            if (splines[ch])
                v = (*splines[ch])(v);
            lut[c][i] = static_cast<float>(v);
        }
    }
    x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *) {
        for (int c = 0; c < 3; ++c) {
            const float pos = clip01(px[c]) * (kLutSize - 1);
            const int i = std::min(static_cast<int>(pos), kLutSize - 2);
            const float t = pos - static_cast<float>(i);
            const std::vector<float> &l = lut[c];
            px[c] = clip01(l[i] + (l[i + 1] - l[i]) * t);
        }
    });
}

void applyEffects(cv::Mat &x, const Effects &effects, quint64 seed)
{
    auto amount = [&](const char *id) { return effectAmount(effects, QLatin1String(id)) / 100.f; };
    const float bw = amount("bw"), sepia = amount("sepia"), vintage = amount("vintage"), fade = amount("fade"),
                matte = amount("matte"), vignette = amount("vignette"), grain = amount("grain");
    const int w = x.cols, h = x.rows;

    // Grade do grão: kGrainCells no lado maior, amostrada com interpolação bilinear.
    int gw = 0, gh = 0;
    std::vector<float> grid;
    if (grain > 0.f) {
        const double longSide = std::max(w, h);
        gw = std::max(1, static_cast<int>(std::lround(kGrainCells * w / longSide)));
        gh = std::max(1, static_cast<int>(std::lround(kGrainCells * h / longSide)));
        grid.resize(static_cast<size_t>(gw) * gh);
        for (int j = 0; j < gh; ++j)
            for (int i = 0; i < gw; ++i)
                grid[static_cast<size_t>(j) * gw + i] = cellNoise(seed, i, j);
    }
    // Vinheta: distância elíptica ao centro (1 = meio do lado, √2 = canto).
    constexpr float kVignetteStart = 0.45f;
    const float kVignetteSpan = std::sqrt(2.f) - kVignetteStart;

    x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *pos) {
        const int row = pos[0], col = pos[1];
        if (bw > 0.f) {
            const float y = 0.3f * px[0] + 0.59f * px[1] + 0.11f * px[2];
            for (int c = 0; c < 3; ++c)
                px[c] += (y - px[c]) * bw;
        }
        if (sepia > 0.f) {
            const float y = luma709(px);
            const cv::Vec3f s(clip01(y * 1.10f + 0.02f), clip01(y * 0.96f + 0.01f), clip01(y * 0.76f));
            for (int c = 0; c < 3; ++c)
                px[c] += (s[c] - px[c]) * sepia;
        }
        if (vintage > 0.f) {
            float y = luma709(px);
            // menos cor, luzes quentes, sombras frias, pretos lavados
            for (int c = 0; c < 3; ++c)
                px[c] = y + (px[c] - y) * (1.f - 0.3f * vintage);
            const float hi = y * y, sh = (1.f - y) * (1.f - y);
            px[0] += vintage * (0.06f * hi - 0.03f * sh);
            px[1] += vintage * (0.02f * hi);
            px[2] += vintage * (-0.06f * hi + 0.05f * sh);
            for (int c = 0; c < 3; ++c)
                px[c] = 0.07f * vintage + clip01(px[c]) * (1.f - 0.1f * vintage);
        }
        if (fade > 0.f) {
            for (int c = 0; c < 3; ++c) {
                const float v = px[c] + (0.5f - px[c]) * 0.18f * fade;
                px[c] = 0.08f * fade + v * (1.f - 0.08f * fade);
            }
        }
        if (matte > 0.f) {
            for (int c = 0; c < 3; ++c) {
                const float inv = 1.f - clip01(px[c]);
                px[c] += 0.16f * matte * inv * inv * inv;
            }
        }
        if (vignette > 0.f) {
            const float u = ((col + 0.5f) - w * 0.5f) / (w * 0.5f);
            const float v = ((row + 0.5f) - h * 0.5f) / (h * 0.5f);
            const float d = clip01((std::sqrt(u * u + v * v) - kVignetteStart) / kVignetteSpan);
            const float gain = 1.f - 0.75f * vignette * d * d * (3.f - 2.f * d);
            for (int c = 0; c < 3; ++c)
                px[c] *= gain;
        }
        if (grain > 0.f) {
            const float gx = std::clamp((col + 0.5f) * gw / w - 0.5f, 0.f, static_cast<float>(gw - 1));
            const float gy = std::clamp((row + 0.5f) * gh / h - 0.5f, 0.f, static_cast<float>(gh - 1));
            const int i0 = static_cast<int>(gx), j0 = static_cast<int>(gy);
            const int i1 = std::min(i0 + 1, gw - 1), j1 = std::min(j0 + 1, gh - 1);
            const float tx = gx - i0, ty = gy - j0;
            auto at = [&](int i, int j) { return grid[static_cast<size_t>(j) * gw + i]; };
            const float n = (at(i0, j0) * (1 - tx) + at(i1, j0) * tx) * (1 - ty)
                + (at(i0, j1) * (1 - tx) + at(i1, j1) * tx) * ty;
            const float y = clip01(luma709(px));
            // Mais forte nos meios-tons, como o grão de filme.
            const float add = n * 0.10f * grain * (0.35f + 2.6f * y * (1.f - y));
            for (int c = 0; c < 3; ++c)
                px[c] += add;
        }
        for (int c = 0; c < 3; ++c)
            px[c] = clip01(px[c]);
    });
}

} // namespace

// --------------------------------------------------------------------------- //
// Canais
// --------------------------------------------------------------------------- //

const char *channelName(Channel c)
{
    return kChannelNames[c];
}

std::optional<Channel> channelByName(const QString &name)
{
    for (int c = 0; c < ChannelCount; ++c) {
        if (name == QLatin1String(kChannelNames[c]))
            return static_cast<Channel>(c);
    }
    return std::nullopt;
}

// --------------------------------------------------------------------------- //
// Níveis
// --------------------------------------------------------------------------- //

LevelsChannel normalizeLevelsChannel(const LevelsChannel &l)
{
    LevelsChannel out;
    out.inBlack = std::clamp(l.inBlack, 0, 253);
    out.inWhite = std::clamp(l.inWhite, 2, 255);
    if (out.inWhite < out.inBlack + 2)
        out.inWhite = out.inBlack + 2;
    double g = std::isfinite(l.gamma) ? l.gamma : 1.0;
    g = std::nearbyint(std::clamp(g, kGammaMin, kGammaMax) * 100.0) / 100.0;
    out.gamma = g;
    out.outBlack = std::clamp(l.outBlack, 0, 255);
    out.outWhite = std::clamp(l.outWhite, 0, 255);
    return out;
}

bool isLevelsIdentity(const Levels &levels)
{
    return std::all_of(levels.begin(), levels.end(), [](const LevelsChannel &l) { return l == kLevelsIdentity; });
}

double applyLevels(const LevelsChannel &l, double x)
{
    double v = std::clamp((x * 255.0 - l.inBlack) / (l.inWhite - l.inBlack), 0.0, 1.0);
    if (l.gamma != 1.0)
        v = std::pow(v, 1.0 / l.gamma);
    return std::clamp((l.outBlack + v * (l.outWhite - l.outBlack)) / 255.0, 0.0, 1.0);
}

QJsonObject levelsChannelToJson(const LevelsChannel &l)
{
    return {{QStringLiteral("inBlack"), l.inBlack},
            {QStringLiteral("gamma"), l.gamma},
            {QStringLiteral("inWhite"), l.inWhite},
            {QStringLiteral("outBlack"), l.outBlack},
            {QStringLiteral("outWhite"), l.outWhite}};
}

LevelsChannel levelsChannelFromJson(const QJsonObject &obj)
{
    LevelsChannel l;
    auto num = [&](const char *key, double fallback) {
        const QJsonValue v = jsonNumber(obj, key);
        return v.isDouble() ? v.toDouble() : fallback;
    };
    l.inBlack = clampInt(num("inBlack", 0), 0, 253, 0);
    l.gamma = num("gamma", 1.0);
    l.inWhite = clampInt(num("inWhite", 255), 2, 255, 255);
    l.outBlack = clampInt(num("outBlack", 0), 0, 255, 0);
    l.outWhite = clampInt(num("outWhite", 255), 0, 255, 255);
    return normalizeLevelsChannel(l);
}

QJsonObject levelsToJson(const Levels &levels)
{
    QJsonObject out;
    for (int c = 0; c < ChannelCount; ++c) {
        if (levels[c] != kLevelsIdentity)
            out.insert(QLatin1String(kChannelNames[c]), levelsChannelToJson(levels[c]));
    }
    return out;
}

Levels levelsFromJson(const QJsonObject &obj)
{
    Levels out{};
    for (int c = 0; c < ChannelCount; ++c) {
        const QJsonValue v = obj.value(QLatin1String(kChannelNames[c]));
        if (v.isObject())
            out[c] = levelsChannelFromJson(v.toObject());
    }
    return out;
}

// --------------------------------------------------------------------------- //
// Curvas
// --------------------------------------------------------------------------- //

Curve normalizeCurve(const Curve &points)
{
    Curve pts;
    for (const CurvePoint &p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        pts.push_back({round4(std::clamp(p.x, 0.0, 1.0)), round4(std::clamp(p.y, 0.0, 1.0))});
    }
    std::stable_sort(pts.begin(), pts.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
    Curve unique;
    for (const CurvePoint &p : pts) {
        if (!unique.empty() && unique.back().x == p.x)
            unique.back() = p;  // x repetido: fica o último
        else
            unique.push_back(p);
    }
    if (static_cast<int>(unique.size()) > kCurveMaxPoints) {
        const CurvePoint last = unique.back();
        unique.resize(kCurveMaxPoints - 1);
        unique.push_back(last);
    }
    if (static_cast<int>(unique.size()) < kCurveMinPoints)
        return {};
    if (unique.size() == 2 && unique[0] == CurvePoint{0.0, 0.0} && unique[1] == CurvePoint{1.0, 1.0})
        return {};
    return unique;
}

bool isCurvesIdentity(const Curves &curves)
{
    return std::all_of(curves.begin(), curves.end(), [](const Curve &c) { return c.empty(); });
}

double evalCurve(const Curve &curve, double x)
{
    return Spline(curve)(x);
}

QJsonArray curveToJson(const Curve &curve)
{
    const Curve &pts = curve.empty() ? Curve{{0.0, 0.0}, {1.0, 1.0}} : curve;
    QJsonArray out;
    for (const CurvePoint &p : pts)
        out.append(QJsonArray{p.x, p.y});
    return out;
}

Curve curveFromJson(const QJsonArray &arr)
{
    Curve pts;
    for (const QJsonValue &v : arr) {
        const QJsonArray xy = v.toArray();
        if (xy.size() == 2 && xy[0].isDouble() && xy[1].isDouble())
            pts.push_back({xy[0].toDouble(), xy[1].toDouble()});
    }
    return normalizeCurve(pts);
}

QJsonObject curvesToJson(const Curves &curves)
{
    QJsonObject out;
    for (int c = 0; c < ChannelCount; ++c) {
        if (!curves[c].empty())
            out.insert(QLatin1String(kChannelNames[c]), curveToJson(curves[c]));
    }
    return out;
}

Curves curvesFromJson(const QJsonObject &obj)
{
    Curves out{};
    for (int c = 0; c < ChannelCount; ++c)
        out[c] = curveFromJson(obj.value(QLatin1String(kChannelNames[c])).toArray());
    return out;
}

// --------------------------------------------------------------------------- //
// Efeitos
// --------------------------------------------------------------------------- //

const std::vector<EffectSpec> &effectCatalog()
{
    static const std::vector<EffectSpec> kCatalog{
        {"bw", 100}, {"sepia", 60}, {"vintage", 50}, {"fade", 40}, {"matte", 50}, {"vignette", 40}, {"grain", 30},
    };
    return kCatalog;
}

bool isEffect(const QString &id)
{
    return std::any_of(effectCatalog().begin(), effectCatalog().end(),
                       [&](const EffectSpec &e) { return id == QLatin1String(e.id); });
}

Effects normalizeEffects(const Effects &effects)
{
    Effects out;
    for (const EffectSpec &spec : effectCatalog()) {
        int amount = 0;
        for (const Effect &e : effects) {
            if (e.id == QLatin1String(spec.id))
                amount = std::clamp(e.amount, 0, 100);
        }
        if (amount > 0)
            out.push_back({QString::fromLatin1(spec.id), amount});
    }
    return out;
}

int effectAmount(const Effects &effects, const QString &id)
{
    for (const Effect &e : effects) {
        if (e.id == id)
            return e.amount;
    }
    return 0;
}

QJsonArray effectsToJson(const Effects &effects)
{
    QJsonArray out;
    for (const Effect &e : effects)
        out.append(QJsonObject{{QStringLiteral("id"), e.id}, {QStringLiteral("amount"), e.amount}});
    return out;
}

Effects effectsFromJson(const QJsonArray &arr)
{
    Effects out;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QJsonValue amount = jsonNumber(o, "amount");
        if (o.value(QStringLiteral("id")).isString() && amount.isDouble())
            out.push_back({o.value(QStringLiteral("id")).toString(), clampInt(amount.toDouble(), 0, 100, 0)});
    }
    return normalizeEffects(out);
}

// --------------------------------------------------------------------------- //
// Look
// --------------------------------------------------------------------------- //

bool Look::isIdentity() const
{
    return isLevelsIdentity(levels) && isCurvesIdentity(curves) && effects.empty();
}

Look normalizeLook(const Look &look)
{
    Look out;
    for (int c = 0; c < ChannelCount; ++c) {
        out.levels[c] = normalizeLevelsChannel(look.levels[c]);
        out.curves[c] = normalizeCurve(look.curves[c]);
    }
    out.effects = normalizeEffects(look.effects);
    return out;
}

Look lookFromJson(const QJsonObject &obj)
{
    Look out;
    out.levels = levelsFromJson(obj.value(QStringLiteral("levels")).toObject());
    out.curves = curvesFromJson(obj.value(QStringLiteral("curves")).toObject());
    out.effects = effectsFromJson(obj.value(QStringLiteral("effects")).toArray());
    return out;
}

void lookToJson(const Look &look, QJsonObject &obj, bool onlyIfSet)
{
    if (!onlyIfSet || !isLevelsIdentity(look.levels))
        obj.insert(QStringLiteral("levels"), levelsToJson(look.levels));
    if (!onlyIfSet || !isCurvesIdentity(look.curves))
        obj.insert(QStringLiteral("curves"), curvesToJson(look.curves));
    if (!onlyIfSet || !look.effects.empty())
        obj.insert(QStringLiteral("effects"), effectsToJson(look.effects));
}

QString lookHashJson(const Look &look)
{
    if (look.isIdentity())
        return {};
    // Chaves em ordem alfabética, como o sort_keys=True: canais b, g, r, rgb.
    const std::array<Channel, ChannelCount> sorted{ChannelBlue, ChannelGreen, ChannelRed, ChannelRgb};
    QStringList parts;
    if (!isCurvesIdentity(look.curves)) {
        QStringList chans;
        for (Channel c : sorted) {
            if (look.curves[c].empty())
                continue;
            QStringList pts;
            for (const CurvePoint &p : look.curves[c])
                pts << QStringLiteral("[") + pyFloat(p.x) + QStringLiteral(", ") + pyFloat(p.y) + QLatin1Char(']');
            chans << QStringLiteral("\"%1\": [%2]").arg(QLatin1String(kChannelNames[c]), pts.join(QStringLiteral(", ")));
        }
        parts << QStringLiteral("\"curves\": {") + chans.join(QStringLiteral(", ")) + QLatin1Char('}');
    }
    if (!look.effects.empty()) {
        QStringList items;
        for (const Effect &e : look.effects)
            items << QStringLiteral("[\"%1\", %2]").arg(e.id).arg(e.amount);
        parts << QStringLiteral("\"effects\": [") + items.join(QStringLiteral(", ")) + QLatin1Char(']');
    }
    if (!isLevelsIdentity(look.levels)) {
        QStringList chans;
        for (Channel c : sorted) {
            const LevelsChannel &l = look.levels[c];
            if (l == kLevelsIdentity)
                continue;
            chans << QStringLiteral("\"%1\": [%2, %3, %4, %5, %6]")
                         .arg(QLatin1String(kChannelNames[c]))
                         .arg(l.inBlack)
                         .arg(pyFloat(l.gamma))
                         .arg(l.inWhite)
                         .arg(l.outBlack)
                         .arg(l.outWhite);
        }
        parts << QStringLiteral("\"levels\": {") + chans.join(QStringLiteral(", ")) + QLatin1Char('}');
    }
    return QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}');
}

quint64 grainSeed(const QString &photoId)
{
    const QByteArray h = QCryptographicHash::hash(photoId.toUtf8(), QCryptographicHash::Sha1);
    quint64 seed = 0;
    for (int i = 0; i < 8; ++i)
        seed = (seed << 8) | static_cast<uchar>(h[i]);
    return seed;
}

void applyLook(cv::Mat &rgb32f, const Look &look, quint64 seed)
{
    CV_Assert(rgb32f.type() == CV_32FC3);
    if (!isLevelsIdentity(look.levels) || !isCurvesIdentity(look.curves))
        applyLuts(rgb32f, look);
    if (!look.effects.empty())
        applyEffects(rgb32f, look.effects, seed);
}

} // namespace pe::imaging

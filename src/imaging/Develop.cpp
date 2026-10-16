// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Develop.h"

#include <QSet>

#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace pe::imaging {

namespace {

// Valor de exp(-mid_tone): 1 no slider move o tom médio (0,42) em ~1 stop.
const double kK = -std::log(kMidTone);
constexpr int kRampSize = 1024;
constexpr int kShadowAnalysisSide = 750;

/// round(x, 2) do Python para valores já no passo (só limpa o ruído binário).
double round2(double x)
{
    return std::nearbyint(x * 100.0) / 100.0;
}

float clip01(float v)
{
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

/// (x * 255 + 0.5).astype(uint8): trunca, como o NumPy.
uchar toByte(float v)
{
    const float s = v * 255.f + 0.5f;
    return s <= 0.f ? 0 : (s >= 255.f ? 255 : static_cast<uchar>(s));
}

} // namespace

const std::array<SliderSpec, SliderCount> &sliders()
{
    static const std::array<SliderSpec, SliderCount> kSliders{{
        {"exposure", -3.0, 3.0, 0.05, "light"},
        {"contrast", -100, 100, 1, "light"},
        {"highlights", -100, 100, 1, "light"},
        {"shadows", -100, 100, 1, "light"},
        {"whites", -100, 100, 1, "light"},
        {"blacks", -100, 100, 1, "light"},
        {"temperature", -100, 100, 1, "color"},
        {"tint", -100, 100, 1, "color"},
        {"vibrance", -100, 100, 1, "color"},
        {"saturation", -100, 100, 1, "color"},
    }};
    return kSliders;
}

std::optional<Slider> sliderByName(const QString &name)
{
    const auto &all = sliders();
    for (int i = 0; i < SliderCount; ++i) {
        if (name == QLatin1String(all[i].name))
            return static_cast<Slider>(i);
    }
    return std::nullopt;
}

const std::vector<Preset> &presets()
{
    // Inspirados nos perfis mais comuns do Lightroom/VSCO.
    static const std::vector<Preset> kPresets{
        {"vivid", {{Contrast, 20}, {Vibrance, 30}, {Saturation, 10}}},
        {"soft", {{Contrast, -20}, {Highlights, -25}, {Shadows, 20}, {Saturation, -10}}},
        {"warm", {{Temperature, 20}, {Vibrance, 10}}},
        {"cool", {{Temperature, -20}, {Tint, -5}}},
        {"bw", {{Saturation, -100}, {Contrast, 20}}},
        {"matte", {{Blacks, 30}, {Contrast, -15}, {Saturation, -20}, {Temperature, 8}}},
        {"vintage", {{Temperature, 15}, {Tint, 5}, {Saturation, -25}, {Blacks, 20}, {Whites, -15}, {Contrast, -10}}},
    };
    return kPresets;
}

bool isPreset(const QString &id)
{
    for (const auto &p : presets()) {
        if (id == QLatin1String(p.id))
            return true;
    }
    return false;
}

bool isIntegerSlider(Slider s)
{
    const double step = sliders()[s].step;
    return step == std::floor(step);
}

double clampValue(Slider s, double value)
{
    const auto &spec = sliders()[s];
    if (!std::isfinite(value))
        return 0.0;
    value = std::clamp(value, spec.min, spec.max);
    // round() do Python arredonda o meio para o par, como o nearbyint.
    value = round2(std::nearbyint(value / spec.step) * spec.step);
    // round() do Python devolve int: nunca há -0.0 (que mudaria o hash).
    if (value == 0.0)
        value = 0.0;
    return isIntegerSlider(s) ? std::trunc(value) + 0.0 : value;
}

Values normalize(const Values &values)
{
    Values out{};
    for (int i = 0; i < SliderCount; ++i)
        out[i] = clampValue(static_cast<Slider>(i), values[i]);
    return out;
}

Values valuesFromJson(const QJsonObject &obj)
{
    Values out{};
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const auto s = sliderByName(it.key());
        if (s && it.value().isDouble())
            out[*s] = clampValue(*s, it.value().toDouble());
        else if (s && it.value().isString()) {
            bool ok = false;
            const double v = it.value().toString().toDouble(&ok);
            if (ok)
                out[*s] = clampValue(*s, v);
        }
    }
    return out;
}

QJsonObject valuesToJson(const Values &values)
{
    QJsonObject obj;
    for (int i = 0; i < SliderCount; ++i) {
        const auto s = static_cast<Slider>(i);
        if (isIntegerSlider(s))
            obj.insert(QLatin1String(sliders()[i].name), static_cast<qint64>(values[i]));
        else
            obj.insert(QLatin1String(sliders()[i].name), values[i]);
    }
    return obj;
}

QString normalizePreset(const QString &preset)
{
    return isPreset(preset) ? preset : QString();
}

Values effective(const Values &values, const QString &preset)
{
    Values out = normalize(values);
    for (const auto &p : presets()) {
        if (preset != QLatin1String(p.id))
            continue;
        for (const auto &[slider, delta] : p.offsets)
            out[slider] = clampValue(slider, out[slider] + delta);
    }
    return out;
}

bool isNeutral(const Values &values, const QString &preset)
{
    const Values v = effective(values, preset);
    return std::all_of(v.begin(), v.end(), [](double x) { return x == 0.0; });
}

bool isMaskKey(const QString &key)
{
    if (key.size() != kMaskKeyLength)
        return false;
    return std::all_of(key.begin(), key.end(), [](QChar c) {
        return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
    });
}

std::vector<Layer> normalizeLayers(const QJsonArray &layers)
{
    std::vector<Layer> out;
    QSet<QString> seen;
    for (int i = 0; i < layers.size(); ++i) {
        const QJsonObject obj = layers.at(i).toObject();
        const QString mask = obj.value(QStringLiteral("mask")).toString();
        if (!layers.at(i).isObject() || !isMaskKey(mask))
            continue;
        QString id = obj.value(QStringLiteral("id")).isString() ? obj.value(QStringLiteral("id")).toString() : QString();
        const bool alnum = std::all_of(id.begin(), id.end(), [](QChar c) { return c.isLetterOrNumber(); });
        if (id.isEmpty() || id.size() > 32 || !alnum || seen.contains(id))
            id = mask.left(8) + QString::number(i);
        seen.insert(id);
        Layer layer;
        layer.id = id;
        layer.mask = mask;
        layer.values = valuesFromJson(obj.value(QStringLiteral("values")).toObject());
        layer.preset = normalizePreset(obj.value(QStringLiteral("preset")).toString());
        layer.look = lookFromJson(obj);
        out.push_back(layer);
        if (static_cast<int>(out.size()) == kMaxLayers)
            break;
    }
    return out;
}

QJsonArray layersToJson(const std::vector<Layer> &layers)
{
    QJsonArray out;
    for (const auto &l : layers) {
        QJsonObject obj{
            {QStringLiteral("id"), l.id},
            {QStringLiteral("mask"), l.mask},
            {QStringLiteral("values"), valuesToJson(l.values)},
            {QStringLiteral("preset"), l.preset},
        };
        lookToJson(l.look, obj, true);
        out.append(obj);
    }
    return out;
}

bool isNeutral(const Values &values, const QString &preset, const std::vector<Layer> &layers)
{
    return isNeutral(values, preset)
        && std::all_of(layers.begin(), layers.end(),
                       [](const Layer &l) { return isNeutral(l.values, l.preset) && l.look.isIdentity(); });
}

// --------------------------------------------------------------------------- //
// Conversões
// --------------------------------------------------------------------------- //

cv::Vec3f wbGains(double temperature, double tint)
{
    // temperature: R sobe, B desce; tint: G desce, R e B sobem (magenta).
    const double t = temperature * kWbScale;
    const double m = tint * kWbScale;
    return {static_cast<float>(std::exp(t + m / 2)), static_cast<float>(std::exp(-m)),
            static_cast<float>(std::exp(-t + m / 2))};
}

std::pair<double, double> gainsToWb(const cv::Vec3d &gains)
{
    cv::Vec3d lg;
    for (int i = 0; i < 3; ++i)
        lg[i] = std::log(std::max(gains[i], kEps));
    const double mean = (lg[0] + lg[1] + lg[2]) / 3.0;
    lg -= cv::Vec3d(mean, mean, mean);
    return {(lg[0] - lg[2]) / 2 / kWbScale, -lg[1] / kWbScale};
}

double exposureGamma(double exposure)
{
    return std::pow(2.0, -exposure / kK);
}

double gammaToExposure(double gamma)
{
    return -kK * std::log2(gamma);
}

cv::Mat luma(const cv::Mat &rgb32f)
{
    cv::Mat y;
    cv::transform(rgb32f, y, cv::Matx13f(0.2126f, 0.7152f, 0.0722f));
    return y;
}

std::vector<float> toneRamp(const Values &v)
{
    std::vector<float> ramp(kRampSize);
    const double gamma = exposureGamma(v[Exposure]);
    const double a = v[Contrast] / 100.0;
    const double hl = v[Highlights] / 100.0;
    const double w = v[Whites] / 100.0;
    const double b = v[Blacks] / 100.0;
    const double inWhite = 1.0 - 0.25 * std::max(w, 0.0);
    const double outWhite = 1.0 + 0.25 * std::min(w, 0.0);
    const double inBlack = 0.15 * std::max(-b, 0.0);
    const double outBlack = 0.15 * std::max(b, 0.0);
    for (int i = 0; i < kRampSize; ++i) {
        double y = static_cast<float>(i) / static_cast<float>(kRampSize - 1);
        y = std::pow(y, gamma);
        // Curva S ancorada em 0,5 (identidade <-> smoothstep). Negativa achata.
        if (a != 0.0)
            y = std::clamp((1.0 - a) * y + a * (y * y * (3.0 - 2.0 * y)), 0.0, 1.0);
        // highlights: sino em [0,5; 1] com pico em 0,75 e zero no branco.
        if (hl != 0.0) {
            const double bump = y > 0.5 ? 16.0 * (y - 0.5) * (1.0 - y) : 0.0;
            y = std::clamp(y + 0.12 * hl * bump, 0.0, 1.0);
        }
        // whites/blacks = levels.
        if (w != 0.0 || b != 0.0) {
            y = std::clamp((y - inBlack) / (inWhite - inBlack), 0.0, 1.0);
            y = outBlack + (outWhite - outBlack) * y;
        }
        ramp[i] = static_cast<float>(y);
    }
    return ramp;
}

cv::Mat guidedFilter(const cv::Mat &p, const cv::Mat &g, int r, double eps)
{
    const cv::Size k(r, r);
    cv::Mat mp, mg, pg, gg;
    cv::boxFilter(p, mp, -1, k);
    cv::boxFilter(g, mg, -1, k);
    cv::boxFilter(p.mul(g), pg, -1, k);
    cv::boxFilter(g.mul(g), gg, -1, k);
    const cv::Mat cov = pg - mp.mul(mg);
    const cv::Mat var = gg - mg.mul(mg);
    cv::Mat a = cov / (var + eps);
    cv::Mat bb = mp - a.mul(mg);
    cv::Mat ma, mb;
    cv::boxFilter(a, ma, -1, k);
    cv::boxFilter(bb, mb, -1, k);
    return ma.mul(g) + mb;
}

namespace {

/// (max - min) / max por pixel: a "saturação" usada pelas sombras e pelo vibrance.
cv::Mat pixelSaturation(const cv::Mat &rgb32f)
{
    cv::Mat sat(rgb32f.size(), CV_32F);
    rgb32f.forEach<cv::Vec3f>([&](const cv::Vec3f &px, const int *pos) {
        const float mx = std::max({px[0], px[1], px[2]});
        const float mn = std::min({px[0], px[1], px[2]});
        sat.at<float>(pos[0], pos[1]) = (mx - mn) / std::max(mx, static_cast<float>(kEps));
    });
    return sat;
}

/// Máscara calculada numa miniatura e ampliada: sombra é estrutura de baixa
/// frequência, e a ampliada correlaciona 0,99 com a de resolução plena.
cv::Mat shadowMaskFull(const cv::Mat &x)
{
    const int h = x.rows, w = x.cols;
    const double s = static_cast<double>(kShadowAnalysisSide) / std::max(h, w);
    if (s >= 1.0)
        return shadowMask(x);
    cv::Mat small;
    cv::resize(x, small, cv::Size(std::max(static_cast<int>(w * s), 1), std::max(static_cast<int>(h * s), 1)), 0, 0,
               cv::INTER_AREA);
    cv::Mat m = shadowMask(small);
    cv::Mat full;
    cv::resize(m, full, cv::Size(w, h), 0, 0, cv::INTER_LINEAR);
    return full;
}

} // namespace

cv::Mat shadowMask(const cv::Mat &rgb32f, double knee, double floor, double satSup)
{
    const cv::Mat y = luma(rgb32f);
    cv::Mat base = guidedFilter(y, y, std::max(std::max(y.rows, y.cols) / 16, 3));
    cv::Mat m(y.size(), CV_32F);
    const float fk = static_cast<float>(knee), ff = static_cast<float>(floor);
    base.forEach<float>([&](float &bv, const int *pos) {
        const float b = clip01(bv);
        const float dark = clip01((fk - b) / fk);
        m.at<float>(pos[0], pos[1]) = std::pow(dark, 1.5f) * clip01(b / ff);
    });
    if (satSup > 0) {
        cv::Mat sat;
        cv::blur(pixelSaturation(rgb32f), sat, cv::Size(15, 15));
        const float fs = static_cast<float>(satSup);
        m.forEach<float>([&](float &mv, const int *pos) { mv *= clip01(1.f - sat.at<float>(pos[0], pos[1]) / fs); });
    }
    return m;
}

cv::Mat render(const cv::Mat &rgb, const Values &values, const QString &preset, const Look &look, quint64 seed)
{
    CV_Assert(rgb.type() == CV_8UC3);
    const Values v = effective(values, preset);
    if (std::all_of(v.begin(), v.end(), [](double x) { return x == 0.0; }) && look.isIdentity())
        return rgb.clone();

    cv::Mat x;
    rgb.convertTo(x, CV_32FC3, 1.0 / 255.0);

    // 1. balanço de branco
    if (v[Temperature] != 0.0 || v[Tint] != 0.0) {
        const cv::Vec3f g = wbGains(v[Temperature], v[Tint]);
        x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *) {
            for (int c = 0; c < 3; ++c)
                px[c] = clip01(px[c] * g[c]);
        });
    }

    // 2. tom na luminância, com ganho multiplicativo (preserva matiz)
    if (v[Exposure] != 0.0 || v[Contrast] != 0.0 || v[Highlights] != 0.0 || v[Whites] != 0.0 || v[Blacks] != 0.0) {
        const std::vector<float> ramp = toneRamp(v);
        x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *) {
            const float y = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
            // np.interp numa grade uniforme de 1024 pontos.
            float yn;
            if (y <= 0.f) {
                yn = ramp.front();
            } else if (y >= 1.f) {
                yn = ramp.back();
            } else {
                const float pos = y * (kRampSize - 1);
                const int i = std::min(static_cast<int>(pos), kRampSize - 2);
                const float t = pos - static_cast<float>(i);
                yn = ramp[i] + (ramp[i + 1] - ramp[i]) * t;
            }
            const float gain = yn / std::max(y, static_cast<float>(kEps));
            for (int c = 0; c < 3; ++c)
                px[c] = clip01(px[c] * gain);
        });
    }

    // 3. sombras locais. O ganho é MULTIPLICATIVO — o único que preserva a
    //    saturação; negativo escurece as sombras pela mesma máscara.
    if (v[Shadows] != 0.0) {
        const cv::Mat m = shadowMaskFull(x);
        const float amount = static_cast<float>(v[Shadows] / 100.0);
        x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *pos) {
            const float y = std::max(0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2], static_cast<float>(kEps));
            const float yn = std::pow(y, 1.f - amount * m.at<float>(pos[0], pos[1]));
            const float gain = yn / y;
            for (int c = 0; c < 3; ++c)
                px[c] = clip01(px[c] * gain);
        });
    }

    // 4. cor: vibrance pesa mais onde a cor é fraca; saturation é uniforme.
    //    -100 = preto e branco.
    if (v[Vibrance] != 0.0 || v[Saturation] != 0.0) {
        const float sat = static_cast<float>(1.0 + v[Saturation] / 100.0);
        const float vib = static_cast<float>(v[Vibrance] / 100.0);
        x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *) {
            const float y = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
            float factor = sat;
            if (vib != 0.f) {
                const float mx = std::max({px[0], px[1], px[2]});
                const float mn = std::min({px[0], px[1], px[2]});
                factor *= 1.f + vib * (1.f - (mx - mn) / std::max(mx, static_cast<float>(kEps)));
            }
            for (int c = 0; c < 3; ++c)
                px[c] = clip01(y + (px[c] - y) * factor);
        });
    }

    // 5-7. níveis, curvas e efeitos (Look.h)
    if (!look.isIdentity())
        applyLook(x, look, seed);

    cv::Mat out(x.size(), CV_8UC3);
    x.forEach<cv::Vec3f>([&](const cv::Vec3f &px, const int *pos) {
        out.at<cv::Vec3b>(pos[0], pos[1]) = cv::Vec3b(toByte(px[0]), toByte(px[1]), toByte(px[2]));
    });
    return out;
}

cv::Mat renderLayers(const cv::Mat &rgb, const Values &values, const QString &preset,
                     const std::vector<LayerPass> &layers, const Look &look, quint64 seed)
{
    cv::Mat out = render(rgb, values, preset, look, seed);
    if (layers.empty())
        return out;
    cv::Mat acc;
    out.convertTo(acc, CV_32FC3);
    for (const auto &layer : layers) {
        CV_Assert(layer.alpha.type() == CV_32F && layer.alpha.size() == rgb.size());
        const cv::Mat top = render(rgb, layer.values, layer.preset, layer.look, seed);
        acc.forEach<cv::Vec3f>([&](cv::Vec3f &a, const int *pos) {
            const cv::Vec3b t = top.at<cv::Vec3b>(pos[0], pos[1]);
            const float al = layer.alpha.at<float>(pos[0], pos[1]);
            for (int c = 0; c < 3; ++c)
                a[c] += (static_cast<float>(t[c]) - a[c]) * al;
        });
    }
    cv::Mat result(acc.size(), CV_8UC3);
    acc.forEach<cv::Vec3f>([&](const cv::Vec3f &a, const int *pos) {
        cv::Vec3b &o = result.at<cv::Vec3b>(pos[0], pos[1]);
        for (int c = 0; c < 3; ++c) {
            const float s = a[c] + 0.5f;
            o[c] = s <= 0.f ? 0 : (s >= 255.f ? 255 : static_cast<uchar>(s));
        }
    });
    return result;
}

} // namespace pe::imaging

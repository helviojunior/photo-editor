// SPDX-License-Identifier: BSD-2-Clause
#include "imaging/Auto.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pe::imaging {

namespace {

constexpr float kEpsF = static_cast<float>(kEps);

// --------------------------------------------------------------------------- //
// Parâmetros por caso de uso
// --------------------------------------------------------------------------- //

// Busca em grade do ../correct-photos (benchmark/grid_casos.py): 9072
// avaliações. wb_strength=1,0 vence nos seis casos; shadow só paga em
// contraluz e subexposta; superexposta quer alvo BAIXO.
constexpr std::array<AutoParams, AutoCaseCount> kParamsByCase{{
    {0.46, 0.70, 1.0, 0.12},  // backlit
    {0.38, 0.00, 1.0, 0.12},  // overexposed
    {0.46, 0.70, 1.0, 0.28},  // underexposed
    {0.46, 0.00, 1.0, 0.12},  // color_cast
    {0.46, 0.00, 1.0, 0.12},  // low_contrast
    {0.46, 0.00, 1.0, 0.28},  // normal
}};

// Limiares calibrados nos percentis do acervo de 2026-09-22 (556 fotos) e
// largura da transição de cada feature, na unidade dela.
constexpr double kBacklitStops = 0.360;
constexpr double kOverClip = 0.068;
constexpr double kOverL = 77.996;
constexpr double kUnderL = 57.985;
constexpr double kCastRb = 0.880;
constexpr double kLowDr = 0.589;

constexpr double kWidthSubjectBg = 0.30;
constexpr double kWidthClip = 0.05;
constexpr double kWidthL = 12.0;
constexpr double kWidthWhitesRb = 0.08;
constexpr double kWidthDr = 0.12;

// --------------------------------------------------------------------------- //
// Utilitários com a semântica do NumPy
// --------------------------------------------------------------------------- //

/// np.quantile(values, q) com a interpolação linear padrão. Reordena ``v``.
double quantile(std::vector<float> &v, double q)
{
    if (v.empty())
        return std::numeric_limits<double>::quiet_NaN();
    const double pos = q * static_cast<double>(v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const double t = pos - static_cast<double>(lo);
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(lo), v.end());
    const double a = v[lo];
    if (lo + 1 >= v.size() || t == 0.0)
        return a;
    const double b = *std::min_element(v.begin() + static_cast<std::ptrdiff_t>(lo) + 1, v.end());
    return a + (b - a) * t;
}

/// Os valores de uma imagem CV_32FC1 (``y.ravel()``) ou só onde ``mask`` != 0.
std::vector<float> values(const cv::Mat &f32, const cv::Mat &mask = {}, bool inverted = false)
{
    std::vector<float> out;
    out.reserve(f32.total());
    for (int r = 0; r < f32.rows; ++r) {
        const float *p = f32.ptr<float>(r);
        const uchar *m = mask.empty() ? nullptr : mask.ptr<uchar>(r);
        for (int c = 0; c < f32.cols; ++c) {
            if (!m || ((m[c] != 0) != inverted))
                out.push_back(p[c]);
        }
    }
    return out;
}

double quantileAll(const cv::Mat &f32, double q)
{
    std::vector<float> v = values(f32);
    return quantile(v, q);
}

/// max e min por pixel (``img.max(axis=2)``, ``img.min(axis=2)``).
void channelMaxMin(const cv::Mat &rgb32f, cv::Mat &mx, cv::Mat &mn)
{
    mx.create(rgb32f.size(), CV_32F);
    mn.create(rgb32f.size(), CV_32F);
    for (int r = 0; r < rgb32f.rows; ++r) {
        const cv::Vec3f *p = rgb32f.ptr<cv::Vec3f>(r);
        float *a = mx.ptr<float>(r);
        float *b = mn.ptr<float>(r);
        for (int c = 0; c < rgb32f.cols; ++c) {
            a[c] = std::max({p[c][0], p[c][1], p[c][2]});
            b[c] = std::min({p[c][0], p[c][1], p[c][2]});
        }
    }
}

/// (max - min) / max(max, EPS): a "saturação" do photofix.
cv::Mat saturation(const cv::Mat &mx, const cv::Mat &mn)
{
    cv::Mat sat(mx.size(), CV_32F);
    for (int r = 0; r < mx.rows; ++r) {
        const float *a = mx.ptr<float>(r);
        const float *b = mn.ptr<float>(r);
        float *s = sat.ptr<float>(r);
        for (int c = 0; c < mx.cols; ++c)
            s[c] = (a[c] - b[c]) / std::max(a[c], kEpsF);
    }
    return sat;
}

/// ``np.clip(img * 255, 0, 255).astype(np.uint8)``: trunca, como o NumPy.
cv::Mat toU8Trunc(const cv::Mat &f32)
{
    cv::Mat out(f32.size(), CV_MAKETYPE(CV_8U, f32.channels()));
    const int n = f32.cols * f32.channels();
    for (int r = 0; r < f32.rows; ++r) {
        const float *p = f32.ptr<float>(r);
        uchar *o = out.ptr<uchar>(r);
        for (int c = 0; c < n; ++c) {
            const float v = std::clamp(p[c] * 255.f, 0.f, 255.f);
            o[c] = static_cast<uchar>(v);
        }
    }
    return out;
}

double ramp(double x, double threshold, double width)
{
    if (std::isnan(x))
        return 0.0;
    return std::clamp((x - threshold) / std::max(width, kEps) + 0.5, 0.0, 1.0);
}

/// ``_whites_rb``: R/B nas superfícies claras e neutras (NaN se poucas).
double whitesRb(const cv::Mat &img)
{
    const cv::Mat y = luma(img);
    cv::Mat mx, mn;
    channelMaxMin(img, mx, mn);
    const cv::Mat sat = saturation(mx, mn);
    double sumR = 0, sumB = 0;
    long count = 0;
    for (int r = 0; r < img.rows; ++r) {
        const cv::Vec3f *p = img.ptr<cv::Vec3f>(r);
        const float *yy = y.ptr<float>(r);
        const float *s = sat.ptr<float>(r);
        const float *m = mx.ptr<float>(r);
        for (int c = 0; c < img.cols; ++c) {
            if (yy[c] > 0.6f && yy[c] < 0.97f && s[c] < 0.20f && m[c] < 0.98f) {
                sumR += p[c][0];
                sumB += p[c][2];
                ++count;
            }
        }
    }
    if (count < 200)
        return std::numeric_limits<double>::quiet_NaN();
    const double meanR = static_cast<double>(static_cast<float>(sumR / count));
    const double meanB = static_cast<double>(static_cast<float>(sumB / count));
    return meanR / std::max(meanB, kEps);
}

const cv::HOGDescriptor &peopleDetector()
{
    static const cv::HOGDescriptor hog = [] {
        cv::HOGDescriptor h;
        h.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
        return h;
    }();
    return hog;
}

} // namespace

QString autoCaseName(AutoCase c)
{
    static const char *const kNames[AutoCaseCount] = {"backlit",    "overexposed",  "underexposed",
                                                      "color_cast", "low_contrast", "normal"};
    return QString::fromLatin1(kNames[c]);
}

// --------------------------------------------------------------------------- //
// enhance.white_patch_neutral
// --------------------------------------------------------------------------- //

// Limiar ABSOLUTO de saturação: com quantil, numa cena toda azulada os
// "menos saturados" eram justamente os que menos revelavam a dominante.
cv::Vec3f whitePatchNeutral(const cv::Mat &rgb32f, double lumLo, double lumHi, double satMax)
{
    CV_Assert(rgb32f.type() == CV_32FC3);
    const cv::Mat y = luma(rgb32f);
    cv::Mat mx, mn;
    channelMaxMin(rgb32f, mx, mn);
    const cv::Mat sat = saturation(mx, mn);
    const float lo = static_cast<float>(lumLo), hi = static_cast<float>(lumHi);
    const float sm = static_cast<float>(satMax);

    cv::Mat band = (y > lo) & (y < hi) & (mx < 0.98f);
    if (cv::countNonZero(band) < static_cast<double>(y.total()) * 0.002) {
        const float q = static_cast<float>(quantileAll(y, 0.80));
        band = (y > q) & (y < hi) & (mx < 0.98f);
    }
    if (cv::countNonZero(band) < 200)
        return {1.f, 1.f, 1.f};
    cv::Mat sel = band & (sat <= sm);
    if (cv::countNonZero(sel) < 200)
        sel = band;
    const cv::Scalar mean = cv::mean(rgb32f, sel);
    cv::Vec3f out;
    for (int c = 0; c < 3; ++c)
        out[c] = std::max(static_cast<float>(mean[c]), kEpsF);
    return out;
}

// --------------------------------------------------------------------------- //
// subject.py
// --------------------------------------------------------------------------- //

// Regiões com detalhe e sem clipping: o fundo estourado do ginásio é liso.
cv::Mat textureMask(const cv::Mat &rgb32f, double clipHi)
{
    const cv::Mat y = luma(rgb32f);
    const cv::Mat g8 = toU8Trunc(y);
    cv::Mat gx, gy, mag, energy;
    cv::Sobel(g8, gx, CV_32F, 1, 0, 3);
    cv::Sobel(g8, gy, CV_32F, 0, 1, 3);
    cv::magnitude(gx, gy, mag);
    cv::blur(mag, energy, cv::Size(31, 31));
    const float q = static_cast<float>(quantileAll(energy, 0.70));
    const float ch = static_cast<float>(clipHi);
    cv::Mat mask = (energy > q) & (y < ch);
    if (cv::countNonZero(mask) < static_cast<double>(y.total()) * 0.01)
        mask = y < ch;
    return mask;
}

cv::Mat hogBoxes(const cv::Mat &rgb32f)
{
    const cv::Mat u8 = toU8Trunc(rgb32f);
    cv::Mat mask = cv::Mat::zeros(rgb32f.size(), CV_8U);
    // Menor que a janela (64x128) o HOG do OpenCV 4.12 corrompe o heap (o
    // Python aborta igual, sem cv2.error para pegar); nenhuma janela caberia.
    if (u8.cols < peopleDetector().winSize.width || u8.rows < peopleDetector().winSize.height)
        return mask;
    std::vector<cv::Rect> rects;
    std::vector<double> weights;
    try {
        peopleDetector().detectMultiScale(u8, rects, weights, 0, cv::Size(8, 8), cv::Size(8, 8), 1.06, 2.0, false);
    } catch (const cv::Exception &) {
        return mask;
    }
    const cv::Rect frame(0, 0, mask.cols, mask.rows);
    for (const cv::Rect &r : rects) {
        const cv::Rect box = r & frame;
        if (!box.empty())
            mask(box).setTo(255);
    }
    return mask;
}

// Região do sujeito SEM viés de gradiente (cor + detector de pessoas).
cv::Mat subjectRegion(const cv::Mat &rgb32f)
{
    cv::Mat mx, mn, sat;
    channelMaxMin(rgb32f, mx, mn);
    cv::blur(saturation(mx, mn), sat, cv::Size(21, 21));
    const float thr = static_cast<float>(std::max(quantileAll(sat, 0.75), 0.12));
    cv::Mat region = sat > thr;
    const double area = static_cast<double>(rgb32f.rows) * rgb32f.cols;
    const cv::Mat boxes = hogBoxes(rgb32f);
    if (cv::countNonZero(boxes) > area * 0.01)
        region |= boxes;
    if (cv::countNonZero(region) < area * 0.01)
        region.setTo(255);
    return region;
}

// --------------------------------------------------------------------------- //
// casos.py — parâmetros por caso de uso, com pertinência suave
// --------------------------------------------------------------------------- //

AutoFeatures autoFeatures(const cv::Mat &rgb32f)
{
    const cv::Mat y = luma(rgb32f);
    const cv::Mat reg = subjectRegion(rgb32f);
    const int inReg = cv::countNonZero(reg);
    const int outReg = static_cast<int>(reg.total()) - inReg;
    std::vector<float> ySub = inReg > 500 ? values(y, reg) : values(y);
    std::vector<float> yBg = outReg > 500 ? values(y, reg, true) : values(y);
    const double a = std::max(static_cast<double>(static_cast<float>(quantile(ySub, 0.5))), 0.005);
    const double b = std::max(static_cast<double>(static_cast<float>(quantile(yBg, 0.5))), 0.005);

    cv::Mat lab;
    cv::cvtColor(rgb32f, lab, cv::COLOR_RGB2Lab);
    std::vector<float> all = values(y);
    const float q95 = static_cast<float>(quantile(all, 0.95));
    const float q05 = static_cast<float>(quantile(all, 0.05));

    AutoFeatures f;
    f.L = cv::mean(lab)[0];
    f.dr = static_cast<double>(q95 - q05);
    f.clip = static_cast<double>(cv::countNonZero(y >= 0.99f)) / static_cast<double>(y.total());
    f.subjectBg = std::log2(b / a);
    f.whitesRb = whitesRb(rgb32f);
    return f;
}

// Pertinência a cada caso, somando 1. Sem fronteira: duas fotos da mesma
// rajada recebem tratamentos parecidos por construção.
std::array<double, AutoCaseCount> autoMemberships(const AutoFeatures &f)
{
    std::array<double, AutoCaseCount> w{};
    w[Backlit] = ramp(f.subjectBg, kBacklitStops, kWidthSubjectBg);
    w[Overexposed] = std::max(ramp(f.clip, kOverClip, kWidthClip), ramp(f.L, kOverL, kWidthL));
    w[Underexposed] = 1.0 - ramp(f.L, kUnderL, kWidthL);
    w[ColorCast] = 1.0 - ramp(f.whitesRb, kCastRb, kWidthWhitesRb);
    w[LowContrast] = 1.0 - ramp(f.dr, kLowDr, kWidthDr);
    w[Normal] = std::max(0.0, 1.0 - *std::max_element(w.begin(), w.begin() + Normal));
    double total = 0;
    for (double v : w)
        total += v;
    if (!(total > kEps)) {
        w.fill(0.0);
        w[Normal] = 1.0;
        return w;
    }
    for (double &v : w)
        v /= total;
    return w;
}

std::pair<AutoParams, AutoCase> autoCaseParams(const cv::Mat &rgb32f)
{
    const auto w = autoMemberships(autoFeatures(rgb32f));
    AutoParams p;
    for (int c = 0; c < AutoCaseCount; ++c) {
        p.target += w[c] * kParamsByCase[c].target;
        p.shadow += w[c] * kParamsByCase[c].shadow;
        p.wbStrength += w[c] * kParamsByCase[c].wbStrength;
        p.contrast += w[c] * kParamsByCase[c].contrast;
    }
    // max(w, key=w.get): no empate, o primeiro na ordem do dicionário.
    const auto dominant = static_cast<AutoCase>(std::max_element(w.begin(), w.end()) - w.begin());
    return {p, dominant};
}

// --------------------------------------------------------------------------- //
// pipeline.estimate -> valores dos sliders
// --------------------------------------------------------------------------- //

AutoResult autoValues(const cv::Mat &rgbU8)
{
    CV_Assert(rgbU8.type() == CV_8UC3 && !rgbU8.empty());
    const int h = rgbU8.rows, w = rgbU8.cols;
    const double s = static_cast<double>(kAutoAnalysisSide) / std::max(h, w);
    cv::Mat small = rgbU8;
    if (s < 1) {
        cv::resize(rgbU8, small,
                   cv::Size(std::max(static_cast<int>(w * s), 1), std::max(static_cast<int>(h * s), 1)), 0, 0,
                   cv::INTER_AREA);
    }
    // astype(float32) / 255.0: divisão em float, como o NumPy.
    cv::Mat x0(small.size(), CV_32FC3);
    for (int r = 0; r < small.rows; ++r) {
        const uchar *p = small.ptr<uchar>(r);
        float *o = x0.ptr<float>(r);
        for (int c = 0; c < small.cols * 3; ++c)
            o[c] = static_cast<float>(p[c]) / 255.f;
    }

    const auto [params, dominant] = autoCaseParams(x0);

    // (a) balanço de branco ancorado em superfícies brancas reais
    const cv::Vec3f illum = whitePatchNeutral(x0);
    const float illumMean = (illum[0] + illum[1] + illum[2]) / 3.f;
    const float strength = static_cast<float>(params.wbStrength);
    cv::Vec3d gain;
    for (int c = 0; c < 3; ++c)
        gain[c] = 1.f + (illumMean / illum[c] - 1.f) * strength;
    const auto [temperature, tint] = gainsToWb(gain);
    const double wbTemperature = clampValue(Temperature, temperature);
    const double wbTint = clampValue(Tint, tint);
    // A exposição é medida na imagem COM o branco que o motor vai aplicar.
    const cv::Vec3f g = wbGains(wbTemperature, wbTint);
    cv::Mat x = x0.clone();
    x.forEach<cv::Vec3f>([&](cv::Vec3f &px, const int *) {
        for (int c = 0; c < 3; ++c)
            px[c] = std::clamp(px[c] * g[c], 0.f, 1.f);
    });

    // (b) exposição nos tons NEUTROS do sujeito (pele, branco do uniforme): uma
    //     camisa roxa é escura por refletância, não por falta de luz.
    const cv::Mat y = luma(x);
    const cv::Mat mask = textureMask(x);
    cv::Mat mx, mn;
    channelMaxMin(x, mx, mn);
    const cv::Mat neutral = mask & (saturation(mx, mn) < 0.60f);
    std::vector<float> ya;
    if (cv::countNonZero(neutral) > 500)
        ya = values(y, neutral);
    else if (cv::countNonZero(mask) > 500)
        ya = values(y, mask);
    else
        ya = values(y);
    const double anchor = std::min(std::max(static_cast<double>(static_cast<float>(quantile(ya, 0.35))), 0.01), 0.99);
    const double target = std::min(std::max(params.target, 0.01), 0.99);
    const double gamma = std::clamp(std::log(target) / std::log(anchor), 0.25, 4.0);  // max_ev = 2

    Values v = kNeutral;
    v[Temperature] = wbTemperature;
    v[Tint] = wbTint;
    v[Exposure] = gammaToExposure(gamma);
    v[Contrast] = params.contrast * 100;
    v[Shadows] = params.shadow * 100;

    AutoResult result;
    result.values = normalize(v);
    result.dominant = dominant;
    result.caseName = autoCaseName(dominant);
    return result;
}

bool isAutoSlider(Slider s)
{
    return s != Vibrance && s != Saturation && s != SliderCount;
}

Values applyAuto(const Values &current, const AutoResult &result)
{
    Values out = current;
    for (int i = 0; i < SliderCount; ++i) {
        if (isAutoSlider(static_cast<Slider>(i)))
            out[i] = result.values[i];
    }
    return out;
}

cv::Rect autoMaskBox(const cv::Mat &maskU8)
{
    CV_Assert(maskU8.type() == CV_8UC1);
    return cv::boundingRect(maskU8 > 127);
}

} // namespace pe::imaging

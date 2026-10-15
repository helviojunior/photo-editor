// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Motor de revelação: original + ajustes -> foto editada.
//
// É o MESMO motor para o preview do editor e para a exportação: o que se vê é o
// que se exporta. Os ajustes nunca tocam o arquivo original; são números no
// banco aplicados sobre os pixels a cada render.
//
// Convenção: imagens são cv::Mat RGB (não BGR) — CV_8UC3 na entrada e na saída,
// CV_32FC3 em [0, 1] no meio. A ordem das etapas segue o photofix_v2:
//
//   1. balanço de branco (temperature/tint)  -> ganho por canal
//   2. curva de tom na LUMINÂNCIA            -> exposure, contrast,
//      (ganho que preserva matiz)               highlights, whites, blacks
//   3. realce local de sombras               -> shadows
//   4. cor                                   -> vibrance, saturation
//
// Porte 1:1 de backend/photoeditor/imaging/develop.py (branch legacy).

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

#include <array>
#include <optional>
#include <vector>

namespace pe::imaging {

// Sobe quando o resultado de um mesmo conjunto de ajustes muda: invalida os
// renders em cache e força a reexportação.
inline constexpr int kEngineVersion = 1;
inline constexpr double kEps = 1e-6;

enum Slider {
    Exposure,
    Contrast,
    Highlights,
    Shadows,
    Whites,
    Blacks,
    Temperature,
    Tint,
    Vibrance,
    Saturation,
    SliderCount
};

struct SliderSpec
{
    const char *name;   ///< nome no banco e na API ("exposure")
    double min;
    double max;
    double step;
    const char *group;  ///< "light" | "color"
};

/// Os dez controles, na ordem do painel.
const std::array<SliderSpec, SliderCount> &sliders();
std::optional<Slider> sliderByName(const QString &name);

/// Valores dos sliders, já no passo e no limite (``normalize``).
using Values = std::array<double, SliderCount>;
inline constexpr Values kNeutral{};

struct Preset
{
    const char *id;
    std::vector<std::pair<Slider, double>> offsets;
};

/// Presets: DESLOCAMENTOS somados aos ajustes da foto, não valores absolutos.
const std::vector<Preset> &presets();
bool isPreset(const QString &id);

/// Valor no limite e no passo do slider (``_clamp``).
double clampValue(Slider s, double value);
/// Sliders de passo inteiro guardam inteiros (o JSON/hash do Python depende disso).
bool isIntegerSlider(Slider s);

Values normalize(const Values &values);
Values valuesFromJson(const QJsonObject &obj);
QJsonObject valuesToJson(const Values &values);
QString normalizePreset(const QString &preset);
/// Ajustes da foto + deslocamentos do preset, limitados aos sliders.
Values effective(const Values &values, const QString &preset);
bool isNeutral(const Values &values, const QString &preset);

// --------------------------------------------------------------------------- //
// Camadas: uma área (máscara PNG em project_data/masks, chave = sha1[:20]) com
// os PRÓPRIOS ajustes. Os ajustes da foto passam a valer só para o restante.
// --------------------------------------------------------------------------- //

inline constexpr int kMaxLayers = 8;
inline constexpr int kMaskKeyLength = 20;

struct Layer
{
    QString id;
    QString mask;
    Values values = kNeutral;
    QString preset;

    bool operator==(const Layer &) const = default;
};

bool isMaskKey(const QString &key);
/// Camadas válidas, na ordem (a de baixo primeiro). Determinístico.
std::vector<Layer> normalizeLayers(const QJsonArray &layers);
QJsonArray layersToJson(const std::vector<Layer> &layers);
bool isNeutral(const Values &values, const QString &preset, const std::vector<Layer> &layers);

// --------------------------------------------------------------------------- //
// Conversões usadas também pelo Auto
// --------------------------------------------------------------------------- //

inline constexpr double kWbScale = 0.005;  ///< 100 no slider = ganho e^0,5 no canal
inline constexpr double kMidTone = 0.42;

cv::Vec3f wbGains(double temperature, double tint);
/// Inverso exato de ``wbGains``: (temperature, tint).
std::pair<double, double> gainsToWb(const cv::Vec3d &gains);
double exposureGamma(double exposure);
double gammaToExposure(double gamma);

/// Luminância Rec.709 de RGB float (CV_32FC3 -> CV_32FC1).
cv::Mat luma(const cv::Mat &rgb32f);
/// Curva de tom resolvida numa rampa de 1024 pontos.
std::vector<float> toneRamp(const Values &v);
/// Guided filter (He, Sun & Tang, 2010) com box filters de lado ``r``.
cv::Mat guidedFilter(const cv::Mat &p, const cv::Mat &g, int r, double eps = 1e-3);
/// Peso do realce de sombras (CV_32FC1) — ``shadow_mask`` do Python.
cv::Mat shadowMask(const cv::Mat &rgb32f, double knee = 0.45, double floor = 0.12, double satSup = 0.7);

/// Aplica os ajustes numa imagem RGB CV_8UC3 e devolve RGB CV_8UC3.
cv::Mat render(const cv::Mat &rgb, const Values &values, const QString &preset = {});

struct LayerPass
{
    Values values = kNeutral;
    QString preset;
    cv::Mat alpha;  ///< CV_32FC1 em [0, 1], do tamanho da imagem
};

/// Restante com values/preset; cada camada revelada do MESMO ``rgb`` e
/// misturada por cima pela alpha. Sem camadas = ``render``.
cv::Mat renderLayers(const cv::Mat &rgb, const Values &values, const QString &preset,
                     const std::vector<LayerPass> &layers);

} // namespace pe::imaging

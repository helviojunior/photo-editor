// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Auto: porte do corretor ``photofix_v2`` do ``../correct-photos`` (TODO 6.3).
//
// O original aplica a correção direto nos pixels. Aqui ela vira VALORES DOS
// SLIDERS — temperature/tint, exposure, contrast e shadows —, que a pessoa
// continua podendo ajustar e que o CTRL+Z desfaz. As conversões são exatas: o
// motor (``render``) reproduz o ganho de branco e o gama que o
// ``pipeline.estimate`` calcularia, a menos do arredondamento do slider.
//
// Fontes portadas:
//
// * ``enhance.white_patch_neutral`` — iluminante medido em superfícies brancas
//   reais da cena (paredes, telhado), não em gray-world;
// * ``subject.texture_mask`` / ``subject.subject_region`` — onde está o sujeito;
// * ``casos.extrair`` / ``pertinencias`` / ``PARAMS_POR_CASO`` — parâmetros por
//   caso de uso (contraluz, superexposta...), com pertinência suave;
// * ``pipeline.estimate`` — exposição ancorada nos tons NEUTROS do sujeito.
//
// Porte 1:1 de backend/photoeditor/imaging/auto.py (branch legacy). A mescla
// com os ajustes gravados (``applyAuto``) e a caixa da camada (``autoMaskBox``)
// vêm de services/editing.py:run_auto.

#include "imaging/Develop.h"

#include <QString>

#include <opencv2/core.hpp>

#include <array>
#include <optional>

namespace pe::imaging {

/// Lado maior da miniatura em que a análise roda (``ANALYSIS_SIDE``).
inline constexpr int kAutoAnalysisSide = 750;

/// Casos de uso, na ordem do dicionário do Python (empate = o primeiro).
enum AutoCase {
    Backlit,
    Overexposed,
    Underexposed,
    ColorCast,
    LowContrast,
    Normal,
    AutoCaseCount
};

/// Nome do caso como o Python o registra ("backlit", "color_cast"...).
QString autoCaseName(AutoCase c);

/// ``Features``: o que decide o caso de uso. ``whitesRb`` pode ser NaN.
struct AutoFeatures
{
    double L = 0;          ///< média do L* (Lab), 0..100
    double dr = 0;         ///< quantil 95 - quantil 5 da luminância
    double clip = 0;       ///< fração de pixels com luminância >= 0,99
    double subjectBg = 0;  ///< stops entre sujeito e fundo (positivo = fundo claro)
    double whitesRb = 0;   ///< R/B nas superfícies que deveriam ser brancas
};

/// ``case_params``: parâmetros ponderados pelas pertinências.
struct AutoParams
{
    double target = 0;
    double shadow = 0;
    double wbStrength = 0;
    double contrast = 0;
};

struct AutoResult
{
    /// Exposure, contrast, highlights, shadows, whites, blacks, temperature e
    /// tint saem do Auto (highlights/whites/blacks = 0); vibrance e saturation
    /// ficam neutros aqui — ``applyAuto`` preserva os da foto.
    Values values = kNeutral;
    AutoCase dominant = Normal;
    QString caseName;  ///< ``autoCaseName(dominant)``
};

// Etapas (expostas para teste). ``rgb32f``: CV_32FC3 em [0, 1].

/// ``white_patch_neutral``: iluminante (média RGB) nos pixels claros e neutros.
cv::Vec3f whitePatchNeutral(const cv::Mat &rgb32f, double lumLo = 0.75, double lumHi = 0.97, double satMax = 0.25);
/// ``texture_mask``: CV_8UC1 (255 = sujeito) — detalhe e sem clipping.
cv::Mat textureMask(const cv::Mat &rgb32f, double clipHi = 0.97);
/// ``_hog_boxes``: CV_8UC1 com as caixas do detector de pessoas (HOG).
cv::Mat hogBoxes(const cv::Mat &rgb32f);
/// ``subject_region``: CV_8UC1 — cor + detector de pessoas.
cv::Mat subjectRegion(const cv::Mat &rgb32f);
AutoFeatures autoFeatures(const cv::Mat &rgb32f);
/// ``memberships``: pertinência a cada caso, somando 1.
std::array<double, AutoCaseCount> autoMemberships(const AutoFeatures &f);
/// ``case_params``: (parâmetros, caso dominante).
std::pair<AutoParams, AutoCase> autoCaseParams(const cv::Mat &rgb32f);

/// ``auto_values``: ajustes de luz e cor para uma imagem RGB CV_8UC3 (o
/// preview JÁ RECORTADO; a análise roda numa miniatura de 750 px).
AutoResult autoValues(const cv::Mat &rgbU8);

/// Sliders que o Auto define (``light_and_wb``): todos menos vibrance e saturation.
bool isAutoSlider(Slider s);

/// ``{**current, **auto}`` do ``run_auto``: luz e branco saem do Auto;
/// vibrance e saturation ficam os de ``current``. Preset, crop e camadas não
/// passam por aqui (ficam como estão).
Values applyAuto(const Values &current, const AutoResult &result);

/// ``_bbox`` do ``run_auto``: caixa (x0, y0, largura, altura) dos pixels > 127
/// de uma máscara CV_8UC1, ou vazia. No Auto de uma camada, a análise roda só
/// nessa caixa da foto inteira.
cv::Rect autoMaskBox(const cv::Mat &maskU8);

} // namespace pe::imaging

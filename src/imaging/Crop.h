// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Crop: o quadro tem uma PROPORÇÃO (``ratio``, altura/largura): 0 = a da própria
// foto (o crop normal do editor); as versões Instagram usam uma das
// instagramRatios(). Com scale 1 e sem giro, o quadro é o maior dessa
// proporção que cabe na foto (cropFrame); scale (0,1..1) o reduz, o centro fica
// em (cx, cy) — frações da largura e da altura — e angle o gira (positivo =
// horário). A foto fica parada; quem gira é o quadro. A saída é o que está sob
// ele, com o conteúdo em pé: 90 graus só troca retrato/paisagem.
//
// O quadro girado tem de caber inteiro na foto: cropMaxScale limita o tamanho
// pelo ângulo e o centro fica a meia caixa das bordas.

#include <QJsonObject>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <utility>
#include <vector>

namespace pe::imaging {

struct Crop
{
    double scale = 1.0;
    double cx = 0.5;
    double cy = 0.5;
    double angle = 0.0;
    double ratio = 0.0;

    bool operator==(const Crop &) const = default;
};

inline constexpr Crop kCropIdentity{};
inline constexpr double kCropMinScale = 0.1;
inline constexpr double kCropMaxAngle = 90.0;
inline constexpr double kInstagramMaxAngle = 45.0;

struct InstagramRatio
{
    const char *id;   ///< "4:5", "1:1", "1.91:1"
    double ratio;     ///< altura/largura
};

/// Proporções que o feed aceita: de 4:5 (retrato) a 1,91:1 (paisagem).
const std::vector<InstagramRatio> &instagramRatios();
std::vector<double> instagramRatioValues();
/// 45° em 4:5 e 1:1 (só endireita); 90° em 1,91:1, como o crop normal.
double instagramMaxAngle(double ratio);
/// Limites do feed para o recorte que sai (altura/largura).
std::pair<double, double> instagramFeedAspect();
bool instagramFeedOk(const Crop &crop, double aspect);

/// Quadro com scale 1, em fração da largura e da altura (0 = a foto inteira).
std::pair<double, double> cropFrame(double ratio, double aspect);
double cropMaxScale(double angle, double aspect, double ratio = 0.0);
/// A proporção de ``ratios`` mais perto de ``target`` (em escala log).
double closestRatio(double target, const std::vector<double> &ratios);

/// Crop válido para uma foto de proporção ``aspect`` (altura/largura). Com
/// ``ratios`` (versão Instagram), ratio é sempre uma delas.
Crop normalizeCrop(const Crop &crop, double aspect, const std::vector<double> &ratios = {});
Crop cropFromJson(const QJsonObject &obj);
QJsonObject cropToJson(const Crop &crop);
bool isCropIdentity(const Crop &crop);
/// Altura/largura do recorte que sai de applyCrop.
double cropOutputAspect(const Crop &crop, double aspect);
/// (quartos de volta, resto em [-45, 45)).
std::pair<int, double> splitAngle(double angle);

/// Recorta o que está sob o quadro (crop já normalizado). Os quartos de volta
/// só trocam a orientação do recorte; o resto endireita o conteúdo.
cv::Mat applyCrop(const cv::Mat &img, const Crop &crop, int interpolation = cv::INTER_LINEAR);

} // namespace pe::imaging

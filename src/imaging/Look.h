// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Acabamento profissional: níveis (levels), curvas e efeitos. Vem DEPOIS das
// etapas do motor (Develop.h) — balanço de branco, tom, sombras e cor — sobre
// a mesma imagem float RGB em [0, 1]:
//
//   5. níveis   (rgb, depois r/g/b)   -> LUT
//   6. curvas   (rgb, depois r/g/b)   -> LUT (cúbica monótona, Fritsch–Carlson)
//   7. efeitos  (ordem FIXA do catálogo: bw, sepia, vintage, fade, matte,
//                vignette, grain)
//
// Níveis e curvas viram UMA LUT por canal (4081 entradas = 16 por nível de
// 8 bits: um pixel que chega intocado cai exatamente num ponto da tabela).
// Tudo identidade/neutro = a etapa nem roda: a saída é byte a byte a de antes
// (por isso o kEngineVersion não subiu).
//
// A vinheta é medida no quadro que o motor recebe — o recorte já feito (o
// developImage recorta antes de revelar) — e o grão tem semente fixa por foto
// (grainSeed) e tamanho proporcional à imagem: preview e exportação mostram o
// mesmo grão, em escalas diferentes.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <opencv2/core.hpp>

#include <array>
#include <optional>
#include <vector>

namespace pe::imaging {

// --------------------------------------------------------------------------- //
// Canais
// --------------------------------------------------------------------------- //

enum Channel { ChannelRgb, ChannelRed, ChannelGreen, ChannelBlue, ChannelCount };

/// "rgb", "r", "g", "b" — as chaves no JSON e na API.
const char *channelName(Channel c);
std::optional<Channel> channelByName(const QString &name);

// --------------------------------------------------------------------------- //
// Níveis (como os do Photoshop, na escala 0..255)
// --------------------------------------------------------------------------- //

struct LevelsChannel
{
    int inBlack = 0;      ///< 0..253
    double gamma = 1.0;   ///< 0,10..9,99 (passo 0,01)
    int inWhite = 255;    ///< 2..255, sempre >= inBlack + 2
    int outBlack = 0;     ///< 0..255
    int outWhite = 255;   ///< 0..255 (menor que outBlack = inverte)

    bool operator==(const LevelsChannel &) const = default;
};

inline constexpr LevelsChannel kLevelsIdentity{};
using Levels = std::array<LevelsChannel, ChannelCount>;

LevelsChannel normalizeLevelsChannel(const LevelsChannel &l);
bool isLevelsIdentity(const Levels &levels);
/// v = clip((x*255 - inB)/(inW - inB)); v = v^(1/gamma); (outB + v*(outW - outB))/255.
double applyLevels(const LevelsChannel &l, double x);
QJsonObject levelsChannelToJson(const LevelsChannel &l);
LevelsChannel levelsChannelFromJson(const QJsonObject &obj);
/// Só os canais fora da identidade.
QJsonObject levelsToJson(const Levels &levels);
Levels levelsFromJson(const QJsonObject &obj);

// --------------------------------------------------------------------------- //
// Curvas
// --------------------------------------------------------------------------- //

struct CurvePoint
{
    double x = 0.0;
    double y = 0.0;

    bool operator==(const CurvePoint &) const = default;
};

/// Pontos ordenados por x, x únicos, em [0, 1]. Vazia = identidade [(0,0),(1,1)].
using Curve = std::vector<CurvePoint>;
using Curves = std::array<Curve, ChannelCount>;

inline constexpr int kCurveMinPoints = 2;
inline constexpr int kCurveMaxPoints = 16;

/// Limita a [0, 1] (4 casas), ordena, desempata x repetido (fica o último),
/// no máximo 16 pontos (o primeiro e o último sempre ficam). Menos de 2 pontos
/// ou exatamente [(0,0),(1,1)] = identidade (vazia).
Curve normalizeCurve(const Curve &points);
bool isCurvesIdentity(const Curves &curves);
/// Valor da curva em x (cúbica monótona; fora dos extremos, constante).
double evalCurve(const Curve &curve, double x);
/// [[x, y], ...]; a identidade sai como [[0, 0], [1, 1]].
QJsonArray curveToJson(const Curve &curve);
Curve curveFromJson(const QJsonArray &arr);
/// Só os canais fora da identidade.
QJsonObject curvesToJson(const Curves &curves);
Curves curvesFromJson(const QJsonObject &obj);

// --------------------------------------------------------------------------- //
// Efeitos
// --------------------------------------------------------------------------- //

struct EffectSpec
{
    const char *id;
    int defaultAmount;
};

/// Biblioteca de efeitos, NA ORDEM em que são aplicados.
const std::vector<EffectSpec> &effectCatalog();
bool isEffect(const QString &id);

struct Effect
{
    QString id;
    int amount = 0;  ///< 1..100 (0 = desligado, não fica na lista)

    bool operator==(const Effect &) const = default;
};

using Effects = std::vector<Effect>;

/// Ordem do catálogo, sem ids desconhecidos/repetidos (fica o último), amount
/// limitado a 0..100 e sem os de amount 0.
Effects normalizeEffects(const Effects &effects);
int effectAmount(const Effects &effects, const QString &id);
/// [{"id": "bw", "amount": 50}, ...]
QJsonArray effectsToJson(const Effects &effects);
Effects effectsFromJson(const QJsonArray &arr);

// --------------------------------------------------------------------------- //
// O conjunto
// --------------------------------------------------------------------------- //

struct Look
{
    Levels levels{};
    Curves curves{};
    Effects effects;

    bool operator==(const Look &) const = default;
    bool isIdentity() const;
};

Look normalizeLook(const Look &look);
/// Lê as chaves "levels", "curves" e "effects" de ``obj`` (ausentes = identidade).
Look lookFromJson(const QJsonObject &obj);
/// Grava as três chaves em ``obj``; com ``onlyIfSet``, só as fora da identidade.
void lookToJson(const Look &look, QJsonObject &obj, bool onlyIfSet = false);
/// JSON no formato do json.dumps do Python (chaves em ordem) para o
/// settings_hash; vazio se identidade.
QString lookHashJson(const Look &look);

/// Semente do grão de uma foto (estável entre execuções e plataformas).
quint64 grainSeed(const QString &photoId);

/// Aplica níveis, curvas e efeitos numa imagem CV_32FC3 em [0, 1], no lugar.
void applyLook(cv::Mat &rgb32f, const Look &look, quint64 seed);

} // namespace pe::imaging

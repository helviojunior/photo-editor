// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Merge: áreas de várias fotos coladas, alinhadas, sobre uma foto base.
//
// O caso de uso é a trajetória da bola: a sequência de cliques da mesma
// jogada, a primeira foto como base e, de cada uma das seguintes, só a bola —
// cada uma numa camada com a sua transparência.
//
// A câmera está na mão: entre um clique e outro o quadro anda alguns pixels,
// gira uma fração de grau e o zoom respira. Colar a área na mesma posição do
// pixel desalinharia o fundo em volta dela. Por isso, antes de extrair, cada
// foto é ALINHADA à base pelo fundo:
//
// * Geometria — pontos SIFT das duas fotos, pareados (teste da razão de Lowe)
//   e filtrados por RANSAC numa transformação de SIMILARIDADE (escala, rotação
//   e translação). Quem se mexeu (jogadores, a bola) sai como outlier; o que
//   sobra é o ginásio parado. Medido na sequência de teste (2026-09-27):
//   ~2.200 pontos de fundo por par e erro mediano de 0,2 px no preview de
//   1600 px. Uma homografia não reduziu o erro — a similaridade, com 4 graus
//   de liberdade, é a mais estável.
// * Luz — com as fotos já alinhadas, os pixels onde as duas coincidem são
//   fundo; a razão das médias deles, por canal, é o ganho que iguala exposição
//   e balanço de branco da área colada ao da base.
//
// As matrizes guardadas estão em pixels da foto INTEIRA (na orientação de
// exibição) de cada lado: servem ao preview e à resolução cheia, trocando só
// a escala (``scaledMatrix``).
//
// Porte 1:1 de backend/photoeditor/imaging/merge.py (branch legacy) e das
// funções PURAS de services/merges.py (sem banco: o serviço fica com quem
// carrega fotos, máscaras e o modelo Merge). Imagens RGB CV_8UC3.
//
//   Python (imaging/merge.py)       C++
//   align                           align
//   gain                            alignGain
//   describe                        describe
//   scaled                          scaledMatrix
//   cutout                          cutout
//   backdrop                        backdrop
//   paste                           paste
//
//   Python (services/merges.py)     C++
//   default_opacities               defaultOpacities
//   _mask_region                    maskRegion
//   align_version                   alignVersion
//   export_name                     mergeExportName
//   backdrop_source (geometria)     maskBounds + layerClearsBaseArea
//   cutout_png (montagem do RGBA)   pieceToRgba

#include <QJsonObject>
#include <QString>

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

namespace pe::imaging {

// Pontos por foto: o ginásio tem textura de sobra (telhas, faixas), e 6.000
// pontos no preview custam ~0,2 s por par.
inline constexpr int kMergeFeatures = 6000;
inline constexpr double kMergeLoweRatio = 0.75;
inline constexpr double kMergeRansacPx = 2.0;
// Menos pontos de fundo que isso = as fotos não são da mesma cena (ou quase
// tudo mudou); a camada fica sem alinhamento e o frontend avisa.
inline constexpr int kMergeMinInliers = 30;
inline constexpr double kMergeGainMin = 0.7;
inline constexpr double kMergeGainMax = 1.4;
// Pixel é "fundo" quando as fotos alinhadas diferem menos que isso (0..255).
inline constexpr float kMergeStaticDiff = 12.f;

// services/merges.py
inline constexpr int kMergeMaxSources = 16;
inline constexpr const char *kMergeExportSuffix = "_merge";
// Opacidade inicial, da base à última camada: o objeto da base bem claro e
// cada foto seguinte mais forte — a trajetória "chega" na última posição.
inline constexpr double kMergeFirstOpacity = 0.25;
inline constexpr double kMergeLastOpacity = 1.0;
// Margem (px da foto inteira) em volta da área ao recortar a origem.
inline constexpr int kMergeCropMargin = 16;

/// Resultado de ``align`` (o dict ``{'matrix', 'inliers', 'error', 'ok', 'gain'}``).
struct MergeAlign
{
    cv::Matx23d matrix = cv::Matx23d(1, 0, 0, 0, 1, 0);
    int inliers = 0;
    std::optional<double> error;  ///< erro mediano (px), round(.., 3); vazio se falhou
    bool ok = false;
    cv::Vec3d gain = cv::Vec3d(1, 1, 1);
};

/// JSON do alinhamento como o Python o grava (sem a chave ``mask``, que é do serviço).
QJsonObject mergeAlignToJson(const MergeAlign &a);
/// Leitura tolerante: chaves ausentes ficam no padrão (identidade, sem ok).
MergeAlign mergeAlignFromJson(const QJsonObject &obj);

struct MergeDescription
{
    double scale = 1;
    double angle = 0;  ///< graus
    double dx = 0;
    double dy = 0;
};

/// Pedaço de uma foto já levado à base: saída de ``cutout``/``backdrop``.
struct MergePiece
{
    cv::Mat rgb;    ///< CV_32FC3 em [0, 255], já com o ganho
    cv::Mat alpha;  ///< CV_32FC1 em [0, 1], do tamanho de ``rgb``
    cv::Point origin;  ///< canto (x0, y0) da caixa na base
};

/// ``align``: transformação que leva ``src`` sobre ``base`` (pixels de cada
/// imagem). ``exclude`` (CV_8UC1 do tamanho de ``src``, > 0 = fora) tira da
/// análise a área que vai ser extraída — o alinhamento usa só o que está EM
/// VOLTA dela. Sem alinhamento confiável, a matriz é a identidade (na escala)
/// e ``ok`` é falso.
MergeAlign align(const cv::Mat &baseRgb, const cv::Mat &srcRgb, const cv::Mat &exclude = {});

/// ``gain``: ganho por canal que iguala a luz de ``src`` (alinhada) à da base,
/// medido só no fundo — onde as duas fotos já coincidem.
cv::Vec3d alignGain(const cv::Mat &baseRgb, const cv::Mat &srcRgb, const cv::Matx23d &matrix);

/// ``describe``: escala, giro (graus) e deslocamento de uma similaridade 2x3.
MergeDescription describe(const cv::Matx23d &matrix);
/// ``describe`` no formato do JSON da API (scale, angle, dx, dy).
QJsonObject describeToJson(const cv::Matx23d &matrix);

/// ``scaled``: a mesma transformação em outra resolução. ``matrix`` leva
/// pixels de ``srcFrom`` (w, h) a ``dstFrom``; a devolvida, de ``srcTo`` a ``dstTo``.
cv::Matx23d scaledMatrix(const cv::Matx23d &matrix, cv::Size srcFrom, cv::Size srcTo, cv::Size dstFrom,
                         cv::Size dstTo);

/// ``cutout``: área da foto levada sobre a base de ``outSize`` (w, h) — só da
/// caixa que ela ocupa na base —, ou vazio se ficou vazia/fora do quadro.
/// ``alpha`` (CV_32FC1 em [0, 1], do tamanho de ``srcRgb``) é a máscara da
/// área; ``srcRgb`` pode ser um recorte da foto que começa em ``offset``. Só a
/// caixa é transformada: na resolução cheia, girar a foto inteira por uma bola
/// custaria 70 MB e um segundo por camada.
std::optional<MergePiece> cutout(const cv::Mat &srcRgb, const cv::Mat &alpha, const cv::Matx23d &matrix,
                                 const cv::Vec3d &gain, cv::Size outSize, cv::Point offset = {0, 0});

/// ``backdrop``: o que a foto de origem mostra ATRÁS de uma área da base.
/// ``alpha`` (CV_32FC1, a máscara da área já recortada na caixa que começa em
/// ``origin`` da base) e a região da origem, alinhada, que cai nessa caixa. É
/// o fundo limpo para esmaecer um objeto da própria base (numa foto seguinte o
/// objeto já saiu dali).
MergePiece backdrop(const cv::Mat &srcRgb, const cv::Mat &alpha, cv::Point origin, const cv::Matx23d &matrix,
                    const cv::Vec3d &gain);

/// ``paste``: compõe ``piece`` sobre ``canvas`` (RGB CV_8UC3, alterado no
/// lugar) com a transparência da camada. Só a caixa passa por float.
void paste(cv::Mat &canvas, const MergePiece &piece, double opacity);

// --------------------------------------------------------------------------- //
// services/merges.py (partes puras)
// --------------------------------------------------------------------------- //

/// ``default_opacities``: ``n`` opacidades de 0,25 a 1,0, em rampa (round 2).
std::vector<double> defaultOpacities(int n);

/// ``_mask_region``: a máscara (CV_8UC1, do preview) levada a uma imagem de
/// ``size`` (w, h) do mesmo quadro, só na caixa da área (com margem):
/// (alpha CV_32FC1, canto), ou vazio se a máscara está vazia. Ampliar a
/// máscara inteira para 24 MP por causa de uma bola custaria 100 MB.
std::optional<std::pair<cv::Mat, cv::Point>> maskRegion(const cv::Mat &maskU8, cv::Size size);

/// ``align_version``: sha1(json.dumps([matrix, gain], separators=(',', ':')))[:10].
/// Ausente = ``null``, como ``align.get('matrix')`` num dict vazio.
QString alignVersion(const std::optional<cv::Matx23d> &matrix, const std::optional<cv::Vec3d> &gain);
/// ``align_version`` direto do JSON gravado (chaves ``matrix`` e ``gain``).
QString alignVersion(const QJsonObject &align);

/// ``export_name``: ``<base>_merge.<ext>`` (stem/suffix do pathlib).
QString mergeExportName(const QString &fileName);

/// Caixa (x0, y0, x1, y1 INCLUSIVOS) dos pixels > ``threshold`` de uma máscara
/// CV_8UC1, ou vazio. Em ``backdrop_source`` o limiar é 2.
std::optional<cv::Vec4i> maskBounds(const cv::Mat &maskU8, int threshold = 2);

/// Geometria do ``backdrop_source``: a área da camada (``layerMask``), levada
/// à base por ``layerToBase`` (pixels da máscara da camada -> da máscara da
/// base, ver ``scaledMatrix``), cai TODA fora da caixa da área da base (com
/// margem ``kMergeCropMargin``)? Falso também se alguma das máscaras está
/// vazia (o serviço trata base vazia como "sem fundo": ``maskBounds`` antes).
bool layerClearsBaseArea(const cv::Mat &baseMask, const cv::Mat &layerMask, const cv::Matx23d &layerToBase);

/// Montagem do PNG do ``cutout_png``: RGBA CV_8UC4 de ``outSize``,
/// transparente fora da área (e com o RGB zerado lá). Sem peça = tudo zero.
cv::Mat pieceToRgba(const std::optional<MergePiece> &piece, cv::Size outSize);

} // namespace pe::imaging

// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Formato de publicação das fotos exportadas. Porte de imaging/publish.py
// (branch legacy), que espelha o ``convert()`` do PhotoE: JPEG numa caixa
// 1920x1080 (na orientação da foto), 72 dpi, qualidade 88, Huffman otimizado
// e progressivo. Diferenças deliberadas em relação ao PhotoE:
//
// * o EXIF é PRESERVADO — o PhotoE lê DateTimeOriginal + SubsecTimeOriginal +
//   OffsetTimeOriginal para ordenar a galeria do evento;
// * a miniatura embutida sai e ``Orientation`` vira 1, porque os pixels já
//   saem girados;
// * ``Software`` identifica este editor: nome, versão e URL.
//
// O render acontece DEPOIS de reduzir para a caixa: revelar 24 MP para jogar
// fora 90 % dos pixels só custaria tempo. Imagens: cv::Mat RGB CV_8UC3.

#include <QByteArray>
#include <QString>

#include <opencv2/core.hpp>

namespace pe::imaging {

inline constexpr int kTargetLongSide = 1920;
inline constexpr int kTargetShortSide = 1080;
inline constexpr int kTargetDpi = 72;
inline constexpr int kJpegQuality = 88;

// Versão Instagram: 1080 px de largura — a resolução do feed — com a altura
// da proporção (1080x1350 em 4:5, 1080x1080, 1080x566 em 1,91:1). Tamanho
// EXATO: o Instagram reamostra o que chega fora disso. Qualidade mais alta
// porque o próprio Instagram recomprime.
inline constexpr int kInstagramWidth = 1080;
inline constexpr int kInstagramQuality = 92;

/// Caixa 1080p na orientação da imagem: 1920x1080, 1080x1920 ou 1080x1080.
cv::Size targetBox(int width, int height);

struct LoadedPhoto
{
    cv::Mat rgb;      ///< já girado conforme o EXIF; vazio se não deu para ler
    QByteArray exif;  ///< APP1 original (``"Exif\0\0"`` + TIFF); vazio se não há
};

/// Pixels RGB já girados + EXIF original. A resolução é a menor (redução na
/// DCT) que ainda enche a caixa DEPOIS do crop: um recorte de 50 % precisa de
/// 3840 px para sair em 1920. ``cropScale`` é a fração do lado menor da foto
/// que o quadro ocupa.
LoadedPhoto load(const QString &path, double cropScale = 1.0, int longSide = kTargetLongSide);

/// Reduz para a caixa de publicação (``ImageOps.contain``); nunca amplia.
cv::Mat fit(const cv::Mat &rgb);

/// (largura, altura) da versão Instagram para um recorte ``aspect``
/// (altura/largura). A altura arredonda PARA CIMA: 1080x565 já passaria de
/// 1,91:1 e o Instagram recortaria; 1080x566 fica dentro.
cv::Size instagramSize(double aspect);

/// Leva o recorte ao tamanho exato ``size`` (reduz ou amplia).
cv::Mat fitExact(const cv::Mat &rgb, cv::Size size);

/// Valor do EXIF ``Software``: ``"<nome> <versão> (<url>)"``.
QString softwareTag();

/// JPEG de publicação: progressivo, Huffman otimizado, 72 dpi no APP0 e o EXIF
/// de ``exifForPublication`` num APP1 logo depois. Vazio se falhar.
QByteArray encode(const cv::Mat &rgb, const QByteArray &exif = {}, const QString &software = {},
                  int quality = kJpegQuality);

} // namespace pe::imaging

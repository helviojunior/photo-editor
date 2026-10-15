// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Leitura de JPEGs (metadados EXIF e pixels já na orientação de exibição) e os
// derivados em cache (preview e thumbnail). Porte de imaging/io.py e da parte
// pura de services/derivatives.py (branch legacy).
//
// Caminhos são QString e os arquivos são lidos com QFile + cv::imdecode —
// nunca cv::imread com std::string, que quebra com nomes não ASCII no Windows.
// Imagens: cv::Mat RGB (não BGR) CV_8UC3.

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <opencv2/core.hpp>

#include <optional>

namespace pe::imaging {

// Derivados (services/derivatives.py).
inline constexpr int kPreviewSide = 1600;
inline constexpr int kThumbnailSide = 240;
inline constexpr int kPreviewQuality = 92;
inline constexpr int kRenderQuality = 90;
inline constexpr int kThumbnailQuality = 82;

/// ``.jpg``/``.jpeg`` (sem diferenciar maiúsculas) e é arquivo.
bool isJpeg(const QString &path);

/// Cabeçalho de um JPEG, lido sem decodificar: dimensões do SOF (como estão
/// gravadas, antes da orientação) e o APP1 Exif (``"Exif\0\0"`` + TIFF).
struct JpegHeader
{
    int width = 0;
    int height = 0;
    QByteArray exif;
};

std::optional<JpegHeader> readJpegHeader(const QString &path);
std::optional<JpegHeader> parseJpegHeader(const QByteArray &data);

struct JpegInfo
{
    int width = 0;          ///< já na orientação de exibição
    int height = 0;
    int orientation = 1;    ///< 1..8
    QDateTime capturedAt;   ///< UTC; inválido se não há
};

/// Dimensões, orientação e data de captura, lendo só o cabeçalho
/// (``read_info``). nullopt se não for um JPEG legível.
std::optional<JpegInfo> readInfo(const QString &path);

/// Gira/espelha conforme a Orientation do EXIF (``ImageOps.exif_transpose``).
cv::Mat applyOrientation(const cv::Mat &img, int orientation);

/// Fator de redução na DCT (1, 2, 4 ou 8) que o ``draft`` do Pillow escolhe
/// para pedir ``request`` x ``request`` de uma imagem ``width`` x ``height``:
/// os dois lados reduzidos continuam >= o pedido.
int draftScale(int width, int height, int request);

/// Tamanho do ``Image.thumbnail((side, side))`` do Pillow: cabe na caixa,
/// mantém a proporção (arredondando para a mais próxima) e nunca amplia.
cv::Size thumbnailSize(cv::Size size, int maxSide);

/// Redimensiona: INTER_AREA para reduzir, INTER_LANCZOS4 para ampliar.
cv::Mat resizeTo(const cv::Mat &img, cv::Size size);

/// Decodifica um JPEG em RGB reduzido por ``scale`` (1, 2, 4, 8) na própria
/// DCT, SEM aplicar a orientação. Vazio se falhar.
cv::Mat decodeRgb(const QByteArray &data, int scale = 1);

/// Abre o JPEG em RGB, girado conforme o EXIF e reduzido a ``maxSide``
/// (0 = tamanho cheio) — ``load_rgb``. A redução começa na DCT (1/2, 1/4,
/// 1/8): abrir 24 MP para gerar uma miniatura custa uma fração do tempo.
/// Vazio se não der para ler.
cv::Mat loadRgb(const QString &path, int maxSide = 0);

/// JPEG em memória (Huffman otimizado; progressivo se pedido). Vazio se falhar.
QByteArray encodeJpeg(const cv::Mat &rgb, int quality, bool progressive = false);

/// Grava em arquivo temporário e renomeia: requisições simultâneas nunca leem
/// um JPEG pela metade.
bool writeAtomic(const QString &path, const QByteArray &data);

/// Preview do ORIGINAL (sem edição), na orientação de exibição.
bool writePreview(const QString &source, const QString &dest);
/// Thumbnail para a filmstrip (gerado a partir do preview).
bool writeThumbnail(const QString &preview, const QString &dest);

} // namespace pe::imaging

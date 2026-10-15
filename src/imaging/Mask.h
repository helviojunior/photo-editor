// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Máscaras das camadas: PNG em tons de cinza (0 = fora, 255 = dentro, borda
// macia) em project_data/masks, na resolução do PREVIEW e sobre a foto
// INTEIRA — antes do crop, como o quadro do crop. O nome do arquivo é o hash
// do conteúdo (sha1 do PNG, 20 dígitos hex): gravar a mesma máscara duas vezes
// dá o mesmo arquivo, e uma máscara nunca muda. Por isso o histórico só guarda
// a chave, e desfazer volta a apontar para a antiga.
//
// A seleção é incremental: cada traço chega com a máscara que já existe
// (``base``) e sai uma nova, somando (Add) ou tirando (Subtract) o objeto sob
// o traço.
//
// Porte da parte sem banco de backend/photoeditor/services/layers.py (branch
// legacy). Diferença conhecida: o PNG do OpenCV não sai byte a byte igual ao
// do Pillow, então a chave de uma máscara NOVA difere da que o Python daria
// para os mesmos pixels. As máscaras já gravadas continuam sendo lidas pela
// chave antiga; só a deduplicação entre as duas versões se perde.

#include "imaging/Crop.h"

#include <QByteArray>
#include <QString>

#include <opencv2/core.hpp>

#include <optional>

namespace pe::imaging {

enum class StrokeMode { Add, Subtract };

/// "add" | "subtract" (os MODES do Python).
std::optional<StrokeMode> strokeModeByName(const QString &name);

/// <dir>/<key>.png
QString maskPath(const QString &masksDir, const QString &key);

/// Grava a máscara (CV_32FC1 em [0, 1]) e devolve a chave; vazia se a
/// gravação falhou. Gravação atômica; se o arquivo já existe, não regrava.
QString saveMask(const QString &masksDir, const cv::Mat &alpha);

/// Máscara CV_8UC1, ou vazia se a chave é inválida ou não existe. Cópia
/// própria: quem chama pode alterá-la.
cv::Mat loadMask(const QString &masksDir, const QString &key);

/// A máscara como PNG branco com transparência: o frontend pinta a cor por
/// cima com CSS (``mask-image``). Vazio se a máscara não existe. Sai em RGBA
/// (o OpenCV não grava cinza + alfa como o "LA" do Pillow) — o canal alfa é o
/// mesmo.
QByteArray overlayPng(const QString &masksDir, const QString &key);

/// Máscara atual (CV_8UC1, ou vazia) + área do traço (CV_32FC1 em [0, 1]) ->
/// nova máscara CV_32FC1; vazia se nada ficou acima de 0,5. Add = máximo;
/// Subtract = min(atual, 1 - área). Atual de outro tamanho conta como vazia.
cv::Mat combineStroke(const cv::Mat &current, const cv::Mat &region, StrokeMode mode);

/// combineStroke sobre a máscara ``baseKey`` (pode ser vazia) e grava: a
/// chave nova, ou vazia se a máscara ficou vazia.
QString applyStroke(const QString &masksDir, const QString &baseKey, const cv::Mat &region, StrokeMode mode);

/// Fração da foto dentro da máscara (> 127); 0 se ela não existe.
double coverage(const QString &masksDir, const QString &key);

/// A máscara levada à foto que vai ser revelada (``_alpha``): do tamanho da
/// foto inteira ``fullSize``, recortada pelo mesmo crop e reduzida a
/// ``outSize``. CV_32FC1 em [0, 1], ou vazia (com aviso no log) se a máscara
/// sumiu — a camada é pulada.
cv::Mat layerAlpha(const QString &masksDir, const QString &key, cv::Size fullSize, const Crop &crop,
                   cv::Size outSize);

} // namespace pe::imaging

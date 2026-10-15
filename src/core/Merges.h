// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Merge de fotos (services/merges.py): a primeira foto marcada é a BASE e cada
// outra vira uma camada alinhada a ela; em cada camada a pessoa seleciona a
// área (a bola) e escolhe a transparência. A base passa a ser a foto do
// merge: a "Editada" parte do merge composto e o Exportar grava
// <base>_merge.jpg. As fotos das camadas saem da filmstrip enquanto o merge
// existir.
//
// As funções daqui são PURAS em relação ao banco: recebem um MergeSnapshot
// (montado pelo Project na thread principal) e podem rodar em qualquer thread.

#include "core/Models.h"
#include "core/Paths.h"
#include "core/Project.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>

#include <opencv2/core.hpp>

#include <optional>

namespace pe {

/// Tudo de que a composição de um merge depende, sem o banco.
struct MergeSnapshot
{
    MergeRecord merge;
    QHash<QString, Photo> photos;  ///< base + camadas, por id (hex)
    ProjectPaths paths;
};

namespace merges {

/// Camadas que entram na composição: visíveis e com área.
std::vector<MergeLayer> readyLayers(const MergeRecord &merge);
/// Camada de onde vem o fundo atrás do objeto da base (``mask`` vazio = a gravada).
std::optional<MergeLayer> backdropSource(const MergeSnapshot &s, const std::optional<QString> &maskKey = std::nullopt);
bool baseFades(const MergeSnapshot &s);
/// Hash de tudo de que a composição depende; '' quando o merge não muda nada.
QString version(const MergeSnapshot &s);
/// Hash curto das áreas e alinhamentos de todas as camadas (o fundo da base).
QString layersVersion(const MergeSnapshot &s);

/// Alinhamento de ``src`` sobre ``base`` medido nos previews e guardado na
/// resolução cheia, com a área já selecionada fora da análise. Pesado (SIFT):
/// roda em segundo plano. JSON no formato gravado ({matrix, inliers, error,
/// ok, gain, mask}).
QJsonObject analyze(const ProjectPaths &paths, const Photo &base, const Photo &src, const QString &maskKey = {});

/// O merge composto no tamanho do preview da base (cache por versão).
QString compositePreviewPath(const MergeSnapshot &s, const QString &version);
/// A área da camada alinhada sobre o PREVIEW da base: PNG RGBA. Na foto da
/// base, o fundo que aparece por trás do objeto dela. Vazio se não há.
QByteArray cutoutPng(const MergeSnapshot &s, const QString &photoId, const QString &maskKey);
/// (RGB na resolução cheia, EXIF da base) — o que o Exportar revela.
std::pair<cv::Mat, QByteArray> renderFull(const MergeSnapshot &s);

} // namespace merges

} // namespace pe

// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Derivados em cache (preview, thumbnail, render) em project_data/cache.
// Gerados sob demanda a partir do original, que nunca é alterado. O nome do
// arquivo carrega o mtime do original: trocar a foto invalida o cache sem
// precisar apagar nada. Os nomes são os mesmos da versão em Python
// (<uuid com hífens>-<mtime_ns>.jpg), então o cache de um projeto já aberto
// por ela continua valendo.
//
// Tudo aqui roda em qualquer thread: gravação atômica (arquivo temporário +
// rename), sem banco.

#include "core/Merges.h"
#include "core/Models.h"
#include "core/Paths.h"

#include <opencv2/core.hpp>

#include <functional>
#include <optional>

namespace pe {

QString previewPath(const ProjectPaths &paths, const Photo &p);
QString thumbnailPath(const ProjectPaths &paths, const Photo &p);
/// Preview do ORIGINAL (1600 px, na orientação de exibição); gera se faltar.
QString ensurePreview(const ProjectPaths &paths, const Photo &p);
/// Thumbnail (240 px) para a filmstrip, a partir do preview.
QString ensureThumbnail(const ProjectPaths &paths, const Photo &p);

/// Foto INTEIRA (antes do crop) + estado -> foto editada: crop, depois ``fit``
/// (a redução da exportação), depois o motor, o look e as camadas. ``seed`` é
/// a do grão: imaging::grainSeed(photo.id) — a mesma no preview e na exportação.
cv::Mat developImage(const cv::Mat &rgb, const EditState &state, const QString &masksDir,
                     const std::function<cv::Mat(const cv::Mat &)> &fit = {}, quint64 seed = 0);

/// O que o editor precisa para mostrar a "Editada" de uma foto — montado na
/// thread principal (renderRequest), executado em qualquer uma (render).
struct RenderRequest
{
    ProjectPaths paths;
    Photo photo;
    EditState state;
    std::optional<MergeSnapshot> merge;  ///< merge de onde a edição parte
    QString mergeVersion;
};

/// De onde a edição parte: o preview, ou o merge composto (e a versão dele).
std::pair<QString, QString> editSource(const RenderRequest &req);
/// Preview EDITADO (crop + motor + camadas), no tamanho do preview. Devolve o
/// caminho do arquivo (o próprio preview quando não há edição).
QString render(const RenderRequest &req);

} // namespace pe

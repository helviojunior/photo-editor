// SPDX-License-Identifier: BSD-2-Clause
#include "core/Derivatives.h"

#include "core/Project.h"
#include "imaging/Auto.h"
#include "imaging/ImageIO.h"
#include "imaging/Mask.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

Q_LOGGING_CATEGORY(lcDerivatives, "photoeditor.derivatives")

namespace pe {

namespace {

QString cacheName(const ProjectPaths &paths, const QString &kind, const Photo &p, const QString &tail = {})
{
    return paths.cacheDir(kind) + u'/' + Project::dashedId(p.id) + u'-' + QString::number(p.mtimeNs) + tail
        + QStringLiteral(".jpg");
}

QString rawOf(const ProjectPaths &paths, const Photo &p)
{
    return paths.rawPath(p.sourceFileName.isEmpty() ? p.fileName : p.sourceFileName);
}

/// Dois pedidos do mesmo derivado ao mesmo tempo (filmstrip + painel) fariam
/// o trabalho duas vezes; a gravação já é atômica, isto só evita o desperdício.
QMutex &generationLock()
{
    static QMutex lock;
    return lock;
}

} // namespace

QString previewPath(const ProjectPaths &paths, const Photo &p)
{
    return cacheName(paths, QStringLiteral("preview"), p);
}

QString thumbnailPath(const ProjectPaths &paths, const Photo &p)
{
    return cacheName(paths, QStringLiteral("thumbnail"), p);
}

QString ensurePreview(const ProjectPaths &paths, const Photo &p)
{
    const QString path = previewPath(paths, p);
    if (QFileInfo(path).isFile())
        return path;
    QMutexLocker locker(&generationLock());
    if (!QFileInfo(path).isFile() && !imaging::writePreview(rawOf(paths, p), path))
        qCWarning(lcDerivatives) << "Could not create the preview of" << p.fileName;
    return path;
}

QString ensureThumbnail(const ProjectPaths &paths, const Photo &p)
{
    const QString path = thumbnailPath(paths, p);
    if (QFileInfo(path).isFile())
        return path;
    const QString preview = ensurePreview(paths, p);
    if (!QFileInfo(path).isFile() && !imaging::writeThumbnail(preview, path))
        qCWarning(lcDerivatives) << "Could not create the thumbnail of" << p.fileName;
    return path;
}

cv::Mat developImage(const cv::Mat &rgb, const EditState &state, const QString &masksDir,
                     const std::function<cv::Mat(const cv::Mat &)> &fit)
{
    cv::Mat out = imaging::applyCrop(rgb, state.crop);
    if (fit)
        out = fit(out);
    std::vector<imaging::LayerPass> passes;
    for (const auto &layer : state.layers) {
        cv::Mat alpha = imaging::layerAlpha(masksDir, layer.mask, rgb.size(), state.crop, out.size());
        if (!alpha.empty())
            passes.push_back({layer.values, layer.preset, alpha});
    }
    return imaging::renderLayers(out, state.values, state.preset, passes);
}

std::pair<QString, QString> editSource(const RenderRequest &req)
{
    if (req.merge && !req.mergeVersion.isEmpty())
        return {merges::compositePreviewPath(*req.merge, req.mergeVersion), req.mergeVersion};
    return {ensurePreview(req.paths, req.photo), QString()};
}

QString render(const RenderRequest &req)
{
    const auto [source, mergeVersion] = editSource(req);
    if (imaging::isCropIdentity(req.state.crop) && req.state.isNeutral())
        return source;
    // O nome carrega o hash dos ajustes efetivos (e a versão do motor e do
    // merge): arrastar um slider de volta a um valor já visto reaproveita o arquivo.
    const QString tag = mergeVersion.isEmpty() ? QString() : QStringLiteral("-m") + mergeVersion;
    const QString path = cacheName(req.paths, QStringLiteral("render"), req.photo,
                                   tag + u'-' + req.state.settingsHash());
    if (QFileInfo(path).isFile())
        return path;
    const cv::Mat rgb = imaging::loadRgb(source);
    if (rgb.empty())
        return source;
    const cv::Mat out = developImage(rgb, req.state, req.paths.masksDir());
    imaging::writeAtomic(path, imaging::encodeJpeg(out, imaging::kRenderQuality));
    return path;
}

// --------------------------------------------------------------------------- //
// Auto (services/editing.py: run_auto)
// --------------------------------------------------------------------------- //

EditState Project::runAuto(const Photo &p, const QString &layerId)
{
    const EditState s = state(p);
    // Na base de um merge, o Auto mede o merge composto.
    RenderRequest req{m_paths, p, s, std::nullopt, {}};
    if (auto m = mergeForPhoto(p)) {
        req.merge = mergeSnapshot(*m);
        req.mergeVersion = merges::version(*req.merge);
    }
    const cv::Mat rgb = imaging::loadRgb(editSource(req).first);
    if (rgb.empty())
        return s;

    for (size_t i = 0; i < s.layers.size(); ++i) {
        if (s.layers[i].id != layerId)
            continue;
        // Com uma camada, mede só a caixa da área dela (na foto inteira, onde
        // a máscara vive) e grava nos ajustes DELA.
        const cv::Mat mask = imaging::loadMask(m_paths.masksDir(), s.layers[i].mask);
        if (mask.empty() || mask.size() != rgb.size())
            return s;
        const cv::Rect box = imaging::autoMaskBox(mask);
        if (box.empty())
            return s;
        const imaging::AutoResult result = imaging::autoValues(rgb(box).clone());
        qCInfo(lcDerivatives) << "Auto on" << p.fileName << "layer" << layerId << ": case=" << result.caseName;
        EditState next = s;
        next.layers[i].values = imaging::applyAuto(s.layers[i].values, result);
        return applyState(p, next, HistoryKind::Auto);
    }
    const imaging::AutoResult result = imaging::autoValues(imaging::applyCrop(rgb, s.crop));
    qCInfo(lcDerivatives) << "Auto on" << p.fileName << ": case=" << result.caseName;
    // Luz e branco saem do Auto; vibrance, saturation, preset, crop e camadas ficam.
    EditState next = s;
    next.values = imaging::applyAuto(s.values, result);
    return applyState(p, next, HistoryKind::Auto);
}

} // namespace pe

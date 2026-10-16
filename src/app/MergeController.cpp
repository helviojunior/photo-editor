// SPDX-License-Identifier: BSD-2-Clause
#include "MergeController.h"

#include "EditorController.h"
#include "ImageProviders.h"
#include "SelectionChain.h"

#include "core/Derivatives.h"
#include "imaging/ImageIO.h"
#include "imaging/Mask.h"
#include "imaging/Merge.h"
#include "imaging/Segment.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(lcMergeUi, "photoeditor.merge.ui")

using namespace pe;

namespace {

constexpr int kSaveDebounceMs = 350;

double round2(double v)
{
    return std::nearbyint(v * 100.0) / 100.0;
}

} // namespace

MergeController::MergeController(EditorController *editor, QObject *parent)
    : QObject(parent)
    , m_editor(editor)
    , m_chain(std::make_shared<SelectionChain>())
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDebounceMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &MergeController::flush);
}

MergeController::~MergeController()
{
    m_chain->pool.waitForDone();
}

void MergeController::fail(const std::exception &e)
{
    if (const auto *action = dynamic_cast<const ActionError *>(&e)) {
        emit failed(action->key(), action->params());
        return;
    }
    qCWarning(lcMergeUi) << "Merge action failed:" << e.what();
    emit failed(QStringLiteral("error.unexpected"), {{QStringLiteral("detail"), QString::fromUtf8(e.what())}});
}

void MergeController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void MergeController::openMerge(const QString &mergeId)
{
    Project *project = m_editor->project();
    if (!project)
        return;
    const auto merge = project->merge(mergeId);
    if (!merge) {
        emit failed(QStringLiteral("merge.notFound"), {});
        return;
    }
    m_merge = project->mergeSnapshot(*merge);
    m_active = m_merge->merge.layers.empty() ? m_merge->merge.base : m_merge->merge.layers.front().photo;
    m_dirty.clear();
    cancelSelection();
    emit changed();
}

void MergeController::reload()
{
    Project *project = m_editor->project();
    if (!project || !m_merge)
        return;
    const auto merge = project->merge(m_merge->merge.id);
    if (!merge) {
        const QString base = m_merge->merge.base;
        m_merge.reset();
        emit changed();
        emit closed(base);
        return;
    }
    m_merge = project->mergeSnapshot(*merge);
    // O que ainda não foi gravado continua valendo na tela.
    for (auto it = m_dirty.begin(); it != m_dirty.end(); ++it) {
        for (auto &e : m_merge->merge.layers) {
            if (e.photo == it.key()) {
                if (it->contains(QStringLiteral("opacity")))
                    e.opacity = it->value(QStringLiteral("opacity")).toDouble();
                if (it->contains(QStringLiteral("visible")))
                    e.visible = it->value(QStringLiteral("visible")).toBool();
            }
        }
        if (it.key() == m_merge->merge.base && it->contains(QStringLiteral("opacity")))
            m_merge->merge.baseOpacity = it->value(QStringLiteral("opacity")).toDouble();
    }
    emit changed();
}

void MergeController::close()
{
    flush();
    cancelSelection();
    const QString base = m_merge ? m_merge->merge.base : QString();
    m_merge.reset();
    emit changed();
    emit closed(base);
}

QString MergeController::previewUrl(const Photo &p) const
{
    const QString key = QStringLiteral("%1-%2").arg(p.id).arg(p.mtimeNs);
    const ProjectPaths paths = m_merge->paths;
    ImageRegistry::instance().put(QStringLiteral("preview"), key,
                                  [paths, p]() { return imagefiles::load(ensurePreview(paths, p)); });
    return QStringLiteral("image://photo/preview/") + key;
}

QString MergeController::thumbUrl(const Photo &p) const
{
    // As camadas de um merge não estão na filmstrip: o editor não registrou a
    // miniatura delas — registra-se aqui.
    const QString key = QStringLiteral("%1-%2").arg(p.id).arg(p.mtimeNs);
    const ProjectPaths paths = m_merge->paths;
    ImageRegistry::instance().put(QStringLiteral("thumb"), key,
                                  [paths, p]() { return imagefiles::load(ensureThumbnail(paths, p)); });
    return QStringLiteral("image://photo/thumb/") + key;
}

QString MergeController::maskUrl(const QString &key) const
{
    if (key.isEmpty() || !m_merge)
        return {};
    const QString dir = m_merge->paths.masksDir();
    ImageRegistry::instance().put(QStringLiteral("mask"), key, [dir, key]() {
        QImage image;
        image.loadFromData(imaging::overlayPng(dir, key), "PNG");
        return image;
    });
    return QStringLiteral("image://photo/mask/") + key;
}

QString MergeController::cutoutUrl(const QString &photoId, const QString &mask) const
{
    if (!m_merge || mask.isEmpty())
        return {};
    // A chave carrega tudo de que o recorte depende (fotos, área, alinhamentos).
    const MergeSnapshot snapshot = *m_merge;
    const QString key = QStringLiteral("%1-%2-%3-%4").arg(snapshot.merge.id, photoId, mask, merges::layersVersion(snapshot));
    ImageRegistry::instance().put(QStringLiteral("cutout"), key, [snapshot, photoId, mask]() {
        QImage image;
        image.loadFromData(merges::cutoutPng(snapshot, photoId, mask), "PNG");
        return image;
    });
    return QStringLiteral("image://photo/cutout/") + key;
}

QVariantMap MergeController::base() const
{
    if (!m_merge)
        return {};
    const Photo &b = m_merge->photos.value(m_merge->merge.base);
    return {{QStringLiteral("id"), b.id},
            {QStringLiteral("fileName"), b.fileName},
            {QStringLiteral("width"), b.width},
            {QStringLiteral("height"), b.height},
            {QStringLiteral("previewUrl"), previewUrl(b)},
            {QStringLiteral("thumbUrl"), thumbUrl(b)}};
}

QVariantMap MergeController::baseLayer() const
{
    if (!m_merge)
        return {};
    const QString mask = m_merge->merge.baseMask;
    return {{QStringLiteral("mask"), mask},
            {QStringLiteral("hasMask"), !mask.isEmpty()},
            {QStringLiteral("maskUrl"), maskUrl(mask)},
            {QStringLiteral("opacity"), m_merge->merge.baseOpacity},
            {QStringLiteral("active"), m_active == m_merge->merge.base},
            {QStringLiteral("cutoutUrl"), cutoutUrl(m_merge->merge.base, mask)}};
}

QVariantList MergeController::layers() const
{
    QVariantList out;
    if (!m_merge)
        return out;
    for (const auto &e : m_merge->merge.layers) {
        const auto it = m_merge->photos.find(e.photo);
        if (it == m_merge->photos.end())
            continue;
        const imaging::MergeAlign align = imaging::mergeAlignFromJson(e.align);
        const auto d = imaging::describe(align.matrix);
        out << QVariantMap{
            {QStringLiteral("photoId"), e.photo},
            {QStringLiteral("fileName"), it->fileName},
            {QStringLiteral("active"), m_active == e.photo},
            {QStringLiteral("available"), it->isActive()},
            {QStringLiteral("thumbUrl"), thumbUrl(*it)},
            {QStringLiteral("previewUrl"), previewUrl(*it)},
            {QStringLiteral("mask"), e.mask},
            {QStringLiteral("hasMask"), !e.mask.isEmpty()},
            {QStringLiteral("maskUrl"), maskUrl(e.mask)},
            {QStringLiteral("opacity"), e.opacity},
            {QStringLiteral("visible"), e.visible},
            {QStringLiteral("cutoutUrl"), cutoutUrl(e.photo, e.mask)},
            {QStringLiteral("align"),
             QVariantMap{{QStringLiteral("scale"), d.scale},
                         {QStringLiteral("angle"), d.angle},
                         {QStringLiteral("dx"), d.dx},
                         {QStringLiteral("dy"), d.dy},
                         {QStringLiteral("ok"), align.ok},
                         {QStringLiteral("inliers"), align.inliers},
                         {QStringLiteral("error"), align.error ? QVariant(*align.error) : QVariant()}}},
        };
    }
    return out;
}

QString MergeController::exportName() const
{
    return m_merge ? imaging::mergeExportName(m_merge->photos.value(m_merge->merge.base).fileName) : QString();
}

void MergeController::setActivePhoto(const QString &id)
{
    if (id == m_active || m_selecting)
        return;
    m_active = Project::normalizeId(id);
    emit changed();
}

QString MergeController::activePreviewUrl() const
{
    if (!m_merge || !m_merge->photos.contains(m_active))
        return {};
    return previewUrl(m_merge->photos.value(m_active));
}

// --------------------------------------------------------------------------- //
// Transparência, visibilidade, remoção
// --------------------------------------------------------------------------- //

void MergeController::setOpacity(const QString &photoId, double opacity)
{
    if (!m_merge)
        return;
    const QString id = Project::normalizeId(photoId);
    opacity = round2(std::clamp(opacity, 0.0, 1.0));
    if (id == m_merge->merge.base)
        m_merge->merge.baseOpacity = opacity;
    for (auto &e : m_merge->merge.layers) {
        if (e.photo == id)
            e.opacity = opacity;
    }
    m_dirty[id].insert(QStringLiteral("opacity"), opacity);
    m_saveTimer.start();
    emit changed();
}

void MergeController::setVisible(const QString &photoId, bool visible)
{
    if (!m_merge)
        return;
    const QString id = Project::normalizeId(photoId);
    for (auto &e : m_merge->merge.layers) {
        if (e.photo == id)
            e.visible = visible;
    }
    m_dirty[id].insert(QStringLiteral("visible"), visible);
    flush();
}

void MergeController::applyRamp(const QString &mode)
{
    if (!m_merge)
        return;
    const int n = static_cast<int>(m_merge->merge.layers.size()) + 1;
    std::vector<double> ramp = imaging::defaultOpacities(n);  // base primeiro
    if (mode == QLatin1String("fadeOut"))
        std::reverse(ramp.begin(), ramp.end());
    else if (mode == QLatin1String("solid"))
        std::fill(ramp.begin(), ramp.end(), 1.0);
    setOpacity(m_merge->merge.base, ramp[0]);
    for (int i = 1; i < n; ++i)
        setOpacity(m_merge->merge.layers[static_cast<size_t>(i - 1)].photo, ramp[static_cast<size_t>(i)]);
    flush();
}

void MergeController::flush()
{
    m_saveTimer.stop();
    if (m_dirty.isEmpty() || !m_merge)
        return;
    QJsonArray items;
    for (auto it = m_dirty.begin(); it != m_dirty.end(); ++it) {
        QJsonObject item = *it;
        item.insert(QStringLiteral("photo"), it.key());
        items.append(item);
    }
    m_dirty.clear();
    saveItems(items);
}

void MergeController::saveItems(const QJsonArray &items)
{
    Project *project = m_editor->project();
    if (!project || !m_merge)
        return;
    try {
        project->updateMerge(m_merge->merge, items);
    } catch (const std::exception &e) {
        fail(e);
    }
    reload();
    m_editor->reload();
}

void MergeController::removeLayer(const QString &photoId)
{
    Project *project = m_editor->project();
    if (!project || !m_merge)
        return;
    flush();
    try {
        if (project->removeMergeLayer(m_merge->merge, photoId)) {
            const QString base = m_merge->merge.base;
            m_merge.reset();
            m_editor->reload(base);
            emit changed();
            emit closed(base);
            return;
        }
    } catch (const std::exception &e) {
        fail(e);
    }
    if (m_active == Project::normalizeId(photoId))
        m_active = m_merge->merge.base;
    reload();
    m_editor->reload();
}

void MergeController::dissolve()
{
    Project *project = m_editor->project();
    if (!project || !m_merge)
        return;
    const QString base = m_merge->merge.base;
    try {
        project->dissolveMerge(m_merge->merge);
    } catch (const std::exception &e) {
        fail(e);
        return;
    }
    m_dirty.clear();
    m_merge.reset();
    m_editor->reload(base);
    emit changed();
    emit closed(base);
}

// --------------------------------------------------------------------------- //
// Área da camada (pincel)
// --------------------------------------------------------------------------- //

QString MergeController::selectionMaskUrl() const
{
    return maskUrl(m_selectionMask);
}

double MergeController::selectionCoverage() const
{
    if (m_selectionMask.isEmpty() || !m_merge)
        return 0.0;
    return imaging::coverage(m_merge->paths.masksDir(), m_selectionMask);
}

void MergeController::beginSelection()
{
    if (!m_merge || !m_merge->photos.contains(m_active))
        return;
    m_selecting = true;
    m_selectionMask = m_active == m_merge->merge.base ? m_merge->merge.baseMask : QString();
    for (const auto &e : m_merge->merge.layers) {
        if (e.photo == m_active)
            m_selectionMask = e.mask;
    }
    {
        QMutexLocker locker(&m_chain->lock);
        m_chain->mask = m_selectionMask;
        m_generation = ++m_chain->generation;
    }
    m_strokesPending = 0;
    if (m_editor->smartAvailable()) {
        const ProjectPaths paths = m_merge->paths;
        const Photo photo = m_merge->photos.value(m_active);
        imaging::Segmenter *seg = m_editor->segmenter();
        m_chain->pool.start([paths, photo, seg]() {
            seg->warm(QStringLiteral("%1-%2").arg(Project::dashedId(photo.id)).arg(photo.mtimeNs),
                      imaging::loadRgb(ensurePreview(paths, photo)));
        });
    }
    emit selectionChanged();
}

void MergeController::stroke(const QVariantList &points, double radius, const QString &mode, bool smart)
{
    if (!m_selecting || !m_merge || points.isEmpty())
        return;
    std::vector<cv::Point2d> pts;
    for (const QVariant &v : points) {
        const QVariantList xy = v.toList();
        if (xy.size() == 2)
            pts.emplace_back(xy[0].toDouble(), xy[1].toDouble());
    }
    if (pts.empty() || pts.size() > 2000)
        return;
    radius = std::clamp(radius, 0.002, 0.5);
    const auto strokeMode = imaging::strokeModeByName(mode).value_or(imaging::StrokeMode::Add);
    const ProjectPaths paths = m_merge->paths;
    const QString masks = paths.masksDir();
    const Photo photo = m_merge->photos.value(m_active);
    imaging::Segmenter *seg = m_editor->segmenter();
    const bool useSmart = smart && m_editor->smartAvailable();
    auto chain = m_chain;
    const int generation = m_generation;
    ++m_strokesPending;
    emit selectionChanged();
    QPointer<MergeController> self(this);
    chain->pool.start([=]() {
        QString key;
        {
            QMutexLocker locker(&chain->lock);
            if (chain->generation != generation)
                return;
            const cv::Mat rgb = imaging::loadRgb(ensurePreview(paths, photo));
            const cv::Mat region = seg->select(
                QStringLiteral("%1-%2").arg(Project::dashedId(photo.id)).arg(photo.mtimeNs), rgb, pts, radius, useSmart);
            key = imaging::applyStroke(masks, chain->mask, region, strokeMode);
            chain->mask = key;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, key, generation]() {
                if (!self)
                    return;
                // Seleção cancelada no meio: o cancelar já zerou o contador.
                if (generation != self->m_generation)
                    return;
                --self->m_strokesPending;
                self->m_selectionMask = key;
                emit self->selectionChanged();
            },
            Qt::QueuedConnection);
    });
}

void MergeController::clearSelection()
{
    {
        QMutexLocker locker(&m_chain->lock);
        m_chain->mask.clear();
    }
    m_selectionMask.clear();
    emit selectionChanged();
}

void MergeController::cancelSelection()
{
    {
        QMutexLocker locker(&m_chain->lock);
        m_generation = ++m_chain->generation;
    }
    m_selecting = false;
    m_selectionMask.clear();
    m_strokesPending = 0;
    emit selectionChanged();
}

void MergeController::applySelection()
{
    Project *project = m_editor->project();
    if (!m_selecting || m_strokesPending > 0 || !project || !m_merge)
        return;
    const QJsonArray items{QJsonObject{{QStringLiteral("photo"), m_active}, {QStringLiteral("mask"), m_selectionMask}}};
    cancelSelection();
    // Área nova numa camada refaz o alinhamento sem ela (SIFT, ~1 s): em
    // segundo plano, e só então grava.
    const auto pending = project->mergePendingAnalyses(m_merge->merge, items);
    if (pending.empty()) {
        saveItems(items);
        return;
    }
    setBusy(true);
    const ProjectPaths paths = m_merge->paths;
    const Photo base = m_merge->photos.value(m_merge->merge.base);
    std::vector<std::pair<Photo, QString>> jobs;
    for (const auto &[photoId, mask] : pending)
        jobs.emplace_back(m_merge->photos.value(photoId), mask);
    auto *watcher = new QFutureWatcher<QHash<QString, QJsonObject>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, items]() {
        watcher->deleteLater();
        setBusy(false);
        Project *project = m_editor->project();
        if (!project || !m_merge)
            return;
        try {
            project->updateMerge(m_merge->merge, items, watcher->result());
        } catch (const std::exception &e) {
            fail(e);
        }
        reload();
        m_editor->reload();
    });
    watcher->setFuture(QtConcurrent::run([paths, base, jobs]() {
        QHash<QString, QJsonObject> aligns;
        for (const auto &[photo, mask] : jobs)
            aligns.insert(photo.id, merges::analyze(paths, base, photo, mask));
        return aligns;
    }));
}

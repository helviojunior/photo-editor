// SPDX-License-Identifier: BSD-2-Clause
#include "EditorController.h"

#include "ImageProviders.h"
#include "SelectionChain.h"

#include "core/Derivatives.h"
#include "imaging/ImageIO.h"
#include "imaging/Mask.h"
#include "imaging/Merge.h"
#include "imaging/Segment.h"

#include <QCollator>
#include <QLoggingCategory>
#include <QMutex>
#include <QRandomGenerator>
#include <QSettings>
#include <QThreadPool>
#include <QtConcurrent>

#include <algorithm>

Q_LOGGING_CATEGORY(lcEditor, "photoeditor.editor")

using namespace pe;


namespace {

constexpr int kRenderDebounceMs = 120;
const QString kSortKey = QStringLiteral("editor/sort");

QVariantMap cropMap(const imaging::Crop &c)
{
    return {{QStringLiteral("scale"), c.scale}, {QStringLiteral("cx"), c.cx}, {QStringLiteral("cy"), c.cy},
            {QStringLiteral("angle"), c.angle}, {QStringLiteral("ratio"), c.ratio}};
}

imaging::Crop cropFrom(const QVariantMap &m, const imaging::Crop &fallback)
{
    imaging::Crop c = fallback;
    c.scale = m.value(QStringLiteral("scale"), c.scale).toDouble();
    c.cx = m.value(QStringLiteral("cx"), c.cx).toDouble();
    c.cy = m.value(QStringLiteral("cy"), c.cy).toDouble();
    c.angle = m.value(QStringLiteral("angle"), c.angle).toDouble();
    c.ratio = m.value(QStringLiteral("ratio"), c.ratio).toDouble();
    return c;
}

QString randomLayerId()
{
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString id;
    for (int i = 0; i < 10; ++i)
        id += QLatin1Char(kAlphabet[QRandomGenerator::global()->bounded(36)]);
    return id;
}

/// Ordem da filmstrip: data (sem data por último, nos dois sentidos) ou nome
/// em ordem natural (IMG_2 antes de IMG_10).
void sortPhotos(std::vector<Photo> &photos, const QString &mode)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto byName = [&](const Photo &a, const Photo &b) { return collator.compare(a.fileName, b.fileName) < 0; };
    if (mode == QLatin1String("name_asc")) {
        std::stable_sort(photos.begin(), photos.end(), byName);
    } else if (mode == QLatin1String("name_desc")) {
        std::stable_sort(photos.begin(), photos.end(), [&](const Photo &a, const Photo &b) { return byName(b, a); });
    } else {
        const bool desc = mode == QLatin1String("date_desc");
        std::stable_sort(photos.begin(), photos.end(), [&](const Photo &a, const Photo &b) {
            if (a.capturedAt.isValid() != b.capturedAt.isValid())
                return a.capturedAt.isValid();
            if (a.capturedAt != b.capturedAt)
                return desc ? a.capturedAt > b.capturedAt : a.capturedAt < b.capturedAt;
            return byName(a, b);
        });
    }
}

} // namespace

EditorController::EditorController(QObject *parent)
    : QObject(parent)
    , m_chain(std::make_shared<SelectionChain>())
{
    m_sortMode = QSettings().value(kSortKey, QStringLiteral("date_asc")).toString();
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(kRenderDebounceMs);
    connect(&m_renderTimer, &QTimer::timeout, this, &EditorController::updateEditedUrl);
}

EditorController::~EditorController()
{
    m_chain->pool.waitForDone();
}

imaging::Segmenter *EditorController::segmenter()
{
    if (!m_segmenter)
        m_segmenter = std::make_unique<imaging::Segmenter>(AppPaths::segmentModelDir());
    return m_segmenter.get();
}

bool EditorController::smartAvailable() const
{
    return imaging::Segmenter::available(AppPaths::segmentModelDir());
}

template<typename Fn>
void EditorController::guarded(Fn &&fn)
{
    if (!m_project)
        return;
    try {
        fn();
    } catch (const std::exception &e) {
        fail(e);
    }
}

void EditorController::fail(const std::exception &e)
{
    if (const auto *action = dynamic_cast<const ActionError *>(&e)) {
        emit failed(action->key(), action->params());
        return;
    }
    qCWarning(lcEditor) << "Action failed:" << e.what();
    emit failed(QStringLiteral("error.unexpected"), {{QStringLiteral("detail"), QString::fromUtf8(e.what())}});
}

// --------------------------------------------------------------------------- //
// Projeto e filmstrip
// --------------------------------------------------------------------------- //

void EditorController::setProject(Project *project)
{
    if (m_exporter) {
        m_exporter->wait();
        m_exporter.reset();
    }
    m_chain->pool.waitForDone();
    m_project = project;
    m_selecting = false;
    m_picking = false;
    m_picked.clear();
    m_activeLayer.clear();
    m_index = -1;
    if (m_project) {
        m_exporter = std::make_unique<Exporter>(m_project);
        connect(m_exporter.get(), &Exporter::progress, this, &EditorController::exportStatusChanged);
        connect(m_exporter.get(), &Exporter::finished, this, [this]() {
            reload();
            emit exportFinished();
        });
    }
    setStatus({});
    reload();
    emit selectionChanged();
    emit pickingChanged();
    emit exportStatusChanged();
}

void EditorController::reload(const QString &focusId)
{
    const QString keep = !focusId.isEmpty() ? Project::normalizeId(focusId)
        : currentPhoto()                    ? currentPhoto()->id
                                            : QString();
    const int oldIndex = m_index;
    if (!m_project) {
        m_model.reset({}, {}, {});
        m_index = -1;
        loadCurrent();
        return;
    }
    std::vector<Photo> photos = m_project->visiblePhotos();
    sortPhotos(photos, m_sortMode);
    QSet<QString> bases, withInstagram;
    for (const auto &m : m_project->liveMerges())
        bases.insert(m.base);
    for (const auto &p : photos) {
        if (p.isInstagram())
            withInstagram.insert(p.instagramOf);
    }
    m_model.reset(std::move(photos), bases, withInstagram);
    m_model.setPicked(m_picked);
    registerThumbnails();
    int index = keep.isEmpty() ? -1 : m_model.indexOf(keep);
    if (index < 0) {
        // A foto saiu (excluída): fica a que ocupou o lugar dela.
        index = std::min(std::max(oldIndex, 0), m_model.count() - 1);
        while (index > 0 && m_model.at(index).isInstagram())
            --index;
    }
    m_index = index;
    loadCurrent();
}

void EditorController::registerThumbnails()
{
    const ProjectPaths paths = m_project->paths();
    for (const Photo &p : m_model.photos()) {
        const QString key = QStringLiteral("%1-%2").arg(p.id).arg(p.mtimeNs);
        ImageRegistry::instance().put(QStringLiteral("thumb"), key,
                                      [paths, p]() { return imagefiles::load(ensureThumbnail(paths, p)); });
        ImageRegistry::instance().put(QStringLiteral("preview"), key,
                                      [paths, p]() { return imagefiles::load(ensurePreview(paths, p)); });
    }
}

const Photo *EditorController::currentPhoto() const
{
    if (m_index < 0 || m_index >= m_model.count())
        return nullptr;
    return &m_model.at(m_index);
}

void EditorController::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_model.count() || index == m_index)
        return;
    if (m_selecting)
        cancelSelection();
    m_index = index;
    m_activeLayer.clear();
    loadCurrent();
}

void EditorController::selectPhoto(const QString &id)
{
    const int index = m_model.indexOf(id);
    if (index >= 0)
        setCurrentIndex(index);
}

void EditorController::next()
{
    for (int i = m_index + 1; i < m_model.count(); ++i) {
        if (!m_model.at(i).isInstagram()) {
            setCurrentIndex(i);
            return;
        }
    }
}

void EditorController::previous()
{
    for (int i = m_index - 1; i >= 0; --i) {
        if (!m_model.at(i).isInstagram()) {
            setCurrentIndex(i);
            return;
        }
    }
}

void EditorController::setSortMode(const QString &mode)
{
    if (mode == m_sortMode)
        return;
    m_sortMode = mode;
    QSettings().setValue(kSortKey, mode);
    emit sortModeChanged();
    reload();
}

void EditorController::loadCurrent()
{
    m_renderTimer.stop();
    m_merge.reset();
    m_mergeVersion.clear();
    const Photo *p = currentPhoto();
    if (p && m_project) {
        try {
            m_saved = m_project->state(*p);
            if (auto merge = m_project->mergeForPhoto(*p)) {
                m_merge = m_project->mergeSnapshot(*merge);
                m_mergeVersion = merges::version(*m_merge);
            }
        } catch (const std::exception &e) {
            fail(e);
            m_saved = {};
        }
    } else {
        m_saved = {};
    }
    m_draft = m_saved;
    if (std::none_of(m_draft.layers.begin(), m_draft.layers.end(),
                     [&](const imaging::Layer &l) { return l.id == m_activeLayer; }))
        m_activeLayer.clear();
    updateEditedUrl();
    emit currentChanged();
    emit stateChanged();
    emit historyChanged();
}

QVariantMap EditorController::current() const
{
    const Photo *p = currentPhoto();
    if (!p)
        return {};
    int position = 0, total = 0;
    for (int i = 0; i < m_model.count(); ++i) {
        const Photo &q = m_model.at(i);
        if (q.isInstagram())
            continue;
        ++total;
        if (i <= m_index || q.id == p->instagramOf)
            position = total;
    }
    if (p->isInstagram()) {
        const int origin = m_model.indexOf(p->instagramOf);
        position = 0;
        for (int i = 0; i <= origin && i < m_model.count(); ++i) {
            if (!m_model.at(i).isInstagram())
                ++position;
        }
    }
    const QModelIndex idx = m_model.index(m_index);
    return {
        {QStringLiteral("id"), p->id},
        {QStringLiteral("fileName"), p->fileName},
        {QStringLiteral("width"), p->width},
        {QStringLiteral("height"), p->height},
        {QStringLiteral("aspect"), p->aspect()},
        {QStringLiteral("capturedAt"), p->capturedAt.isValid() ? QVariant(p->capturedAt.toLocalTime()) : QVariant()},
        {QStringLiteral("isCopy"), p->isCopy() && !p->isInstagram()},
        {QStringLiteral("isInstagram"), p->isInstagram()},
        {QStringLiteral("instagramOf"), p->instagramOf},
        {QStringLiteral("hasInstagram"), m_model.data(idx, PhotoListModel::HasInstagramRole)},
        {QStringLiteral("isCover"), p->isCover},
        {QStringLiteral("isMergeBase"), m_merge.has_value() && !p->isInstagram()},
        {QStringLiteral("mergeId"), m_merge ? m_merge->merge.id : QString()},
        {QStringLiteral("instagramName"), p->isInstagram() ? QStringLiteral("instagram/") + p->fileName : QString()},
        {QStringLiteral("position"), position},
        {QStringLiteral("total"), total},
    };
}

QString EditorController::originalUrl() const
{
    const Photo *p = currentPhoto();
    if (!p)
        return {};
    return QStringLiteral("image://photo/preview/%1-%2").arg(p->id).arg(p->mtimeNs);
}

void EditorController::setStatus(const QString &key, const QVariantMap &params)
{
    m_status = key;
    m_statusParams = params;
    emit statusMessageChanged();
}

void EditorController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

// --------------------------------------------------------------------------- //
// Render da "Editada"
// --------------------------------------------------------------------------- //

void EditorController::scheduleRender()
{
    m_renderTimer.start();
}

void EditorController::updateEditedUrl()
{
    const Photo *p = currentPhoto();
    if (!p || !m_project) {
        m_editedUrl.clear();
        emit editedUrlChanged();
        return;
    }
    // A chave carrega tudo de que o render depende: a URL só muda quando o
    // resultado muda, e o QML não pede de novo o que já tem.
    QString hash = m_draft.settingsHash();
    for (const auto &l : m_draft.layers)
        hash += u'.' + l.mask.left(6);
    const QString key = QStringLiteral("%1-%2-%3-%4").arg(p->id).arg(p->mtimeNs).arg(hash, m_mergeVersion);
    RenderRequest req{m_project->paths(), *p, m_draft, m_merge, m_mergeVersion};
    ImageRegistry::instance().put(QStringLiteral("render"), key,
                                  [req]() { return imagefiles::load(pe::render(req)); });
    const QString url = QStringLiteral("image://photo/render/") + key;
    if (url != m_editedUrl) {
        m_editedUrl = url;
        emit editedUrlChanged();
    }
}

// --------------------------------------------------------------------------- //
// Revelação
// --------------------------------------------------------------------------- //

imaging::Values &EditorController::draftValues()
{
    for (auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            return l.values;
    }
    return m_draft.values;
}

QString &EditorController::draftPreset()
{
    for (auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            return l.preset;
    }
    return m_draft.preset;
}

QVariantList EditorController::sliders() const
{
    QVariantList out;
    for (const auto &s : imaging::sliders()) {
        out << QVariantMap{{QStringLiteral("name"), QString::fromLatin1(s.name)},
                           {QStringLiteral("min"), s.min},
                           {QStringLiteral("max"), s.max},
                           {QStringLiteral("step"), s.step},
                           {QStringLiteral("group"), QString::fromLatin1(s.group)}};
    }
    return out;
}

QVariantList EditorController::presets() const
{
    QVariantList out;
    for (const auto &p : imaging::presets()) {
        QVariantMap values;
        for (const auto &[slider, delta] : p.offsets)
            values.insert(QString::fromLatin1(imaging::sliders()[slider].name), delta);
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(p.id)}, {QStringLiteral("values"), values}};
    }
    return out;
}

QVariantList EditorController::instagramRatios() const
{
    QVariantList out;
    for (const auto &r : imaging::instagramRatios()) {
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(r.id)},
                           {QStringLiteral("ratio"), r.ratio},
                           {QStringLiteral("maxAngle"), imaging::instagramMaxAngle(r.ratio)}};
    }
    return out;
}

double EditorController::cropMinScale() const
{
    return imaging::kCropMinScale;
}

int EditorController::maxLayers() const
{
    return imaging::kMaxLayers;
}

QVariantMap EditorController::values() const
{
    const imaging::Values *v = &m_draft.values;
    for (const auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            v = &l.values;
    }
    QVariantMap out;
    for (int i = 0; i < imaging::SliderCount; ++i)
        out.insert(QString::fromLatin1(imaging::sliders()[i].name), (*v)[i]);
    return out;
}

QString EditorController::preset() const
{
    for (const auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            return l.preset;
    }
    return m_draft.preset;
}

QVariantMap EditorController::crop() const
{
    return cropMap(m_draft.crop);
}

bool EditorController::cropIsIdentity() const
{
    return imaging::isCropIdentity(m_draft.crop);
}

double EditorController::cropMaxAngle() const
{
    const Photo *p = currentPhoto();
    if (p && p->isInstagram())
        return imaging::instagramMaxAngle(m_draft.crop.ratio);
    return imaging::kCropMaxAngle;
}

bool EditorController::instagramFeedOk() const
{
    const Photo *p = currentPhoto();
    return !p || imaging::instagramFeedOk(m_draft.crop, p->aspect());
}

QString EditorController::maskUrl(const QString &key) const
{
    if (key.isEmpty() || !m_project)
        return {};
    const QString dir = m_project->masksDir();
    ImageRegistry::instance().put(QStringLiteral("mask"), key, [dir, key]() {
        QImage image;
        image.loadFromData(imaging::overlayPng(dir, key), "PNG");
        return image;
    });
    return QStringLiteral("image://photo/mask/") + key;
}

QVariantList EditorController::layers() const
{
    QVariantList out;
    for (const auto &l : m_draft.layers) {
        out << QVariantMap{{QStringLiteral("id"), l.id},
                           {QStringLiteral("mask"), l.mask},
                           {QStringLiteral("maskUrl"), maskUrl(l.mask)},
                           {QStringLiteral("preset"), l.preset}};
    }
    return out;
}

void EditorController::setActiveLayer(const QString &id)
{
    if (id == m_activeLayer)
        return;
    const bool exists = id.isEmpty()
        || std::any_of(m_draft.layers.begin(), m_draft.layers.end(), [&](const auto &l) { return l.id == id; });
    if (!exists)
        return;
    m_activeLayer = id;
    emit stateChanged();
}

QString EditorController::activeLayerMaskUrl() const
{
    for (const auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            return maskUrl(l.mask);
    }
    return {};
}

void EditorController::cycleLayer()
{
    if (m_draft.layers.empty())
        return;
    if (m_activeLayer.isEmpty()) {
        setActiveLayer(m_draft.layers.front().id);
        return;
    }
    for (size_t i = 0; i < m_draft.layers.size(); ++i) {
        if (m_draft.layers[i].id == m_activeLayer) {
            setActiveLayer(i + 1 < m_draft.layers.size() ? m_draft.layers[i + 1].id : QString());
            return;
        }
    }
}

void EditorController::setValue(const QString &name, double value)
{
    const auto slider = imaging::sliderByName(name);
    if (!slider || !currentPhoto())
        return;
    draftValues()[*slider] = imaging::clampValue(*slider, value);
    emit stateChanged();
    scheduleRender();
}

void EditorController::saveDraft(bool withCrop, bool withLayers)
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    guarded([&]() {
        m_saved = m_project->setAdjustments(*p, m_draft.values, m_draft.preset,
                                            withCrop ? std::optional(m_draft.crop) : std::nullopt,
                                            withLayers ? std::optional(imaging::layersToJson(m_draft.layers))
                                                       : std::nullopt);
    });
    // O que o banco aceitou (normalizado) vira o rascunho.
    const imaging::Crop keepCrop = withCrop ? m_saved.crop : m_draft.crop;
    m_draft = m_saved;
    m_draft.crop = keepCrop;
    updateEditedUrl();
    emit stateChanged();
    emit historyChanged();
}

void EditorController::commit()
{
    m_renderTimer.stop();
    if (!(m_draft == m_saved))
        saveDraft(false, true);
    else
        updateEditedUrl();
}

void EditorController::togglePreset(const QString &id)
{
    if (!currentPhoto())
        return;
    QString &preset = draftPreset();
    preset = preset == id ? QString() : imaging::normalizePreset(id);
    saveDraft(false, true);
}

void EditorController::autoAdjust()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    setBusy(true);
    guarded([&]() { m_project->runAuto(*p, m_activeLayer); });
    setBusy(false);
    loadCurrent();
}

void EditorController::reset()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    guarded([&]() { m_project->reset(*p); });
    m_activeLayer.clear();
    loadCurrent();
}

QVariantMap EditorController::normalizeCrop(const QVariantMap &crop) const
{
    const Photo *p = currentPhoto();
    if (!p || !m_project)
        return crop;
    return cropMap(imaging::normalizeCrop(cropFrom(crop, m_draft.crop), p->aspect(), m_project->cropRatios(*p)));
}

double EditorController::cropMaxScaleFor(double angle) const
{
    const Photo *p = currentPhoto();
    return p ? imaging::cropMaxScale(angle, p->aspect(), m_draft.crop.ratio) : 1.0;
}

void EditorController::setCrop(const QVariantMap &crop)
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    m_draft.crop = imaging::normalizeCrop(cropFrom(crop, m_draft.crop), p->aspect(), m_project->cropRatios(*p));
    emit stateChanged();
    scheduleRender();
}

void EditorController::commitCrop()
{
    m_renderTimer.stop();
    if (!(m_draft.crop == m_saved.crop))
        saveDraft(true, true);
    else
        updateEditedUrl();
}

void EditorController::resetCrop()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    imaging::Crop identity;
    identity.ratio = m_draft.crop.ratio;  // a versão Instagram fica na proporção
    m_draft.crop = imaging::normalizeCrop(identity, p->aspect(), m_project->cropRatios(*p));
    saveDraft(true, true);
}

void EditorController::setCropRatio(const QString &id)
{
    const Photo *p = currentPhoto();
    if (!p || !p->isInstagram())
        return;
    for (const auto &r : imaging::instagramRatios()) {
        if (id == QLatin1String(r.id)) {
            imaging::Crop c = m_draft.crop;
            c.ratio = r.ratio;
            m_draft.crop = imaging::normalizeCrop(c, p->aspect(), m_project->cropRatios(*p));
            saveDraft(true, true);
            return;
        }
    }
}

// --------------------------------------------------------------------------- //
// Camadas e seleção
// --------------------------------------------------------------------------- //

void EditorController::deleteLayer(const QString &id)
{
    std::erase_if(m_draft.layers, [&](const imaging::Layer &l) { return l.id == id; });
    if (m_activeLayer == id)
        m_activeLayer.clear();
    saveDraft(false, true);
}

QString EditorController::selectionMaskUrl() const
{
    return maskUrl(m_selectionMask);
}

double EditorController::selectionCoverage() const
{
    if (m_selectionMask.isEmpty() || !m_project)
        return 0.0;
    return imaging::coverage(m_project->masksDir(), m_selectionMask);
}

void EditorController::beginSelection(const QString &layerId)
{
    const Photo *p = currentPhoto();
    if (!p || !m_project)
        return;
    if (layerId.isEmpty() && static_cast<int>(m_draft.layers.size()) >= imaging::kMaxLayers)
        return;
    m_selecting = true;
    m_selectionLayer = layerId;
    m_selectionMask.clear();
    for (const auto &l : m_draft.layers) {
        if (l.id == layerId)
            m_selectionMask = l.mask;
    }
    {
        QMutexLocker locker(&m_chain->lock);
        m_chain->mask = m_selectionMask;
        ++m_chain->generation;
    }
    m_selectionGeneration = m_chain->generation;
    m_strokesPending = 0;
    // Prepara o modelo para a foto: o primeiro traço não paga o encoder.
    if (smartAvailable()) {
        const ProjectPaths paths = m_project->paths();
        const Photo photo = *p;
        imaging::Segmenter *seg = segmenter();
        m_chain->pool.start([paths, photo, seg]() {
            seg->warm(QStringLiteral("%1-%2").arg(Project::dashedId(photo.id)).arg(photo.mtimeNs),
                      imaging::loadRgb(ensurePreview(paths, photo)));
        });
    }
    emit selectionChanged();
}

void EditorController::stroke(const QVariantList &points, double radius, const QString &mode, bool smart)
{
    const Photo *p = currentPhoto();
    if (!m_selecting || !p || points.isEmpty())
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
    const ProjectPaths paths = m_project->paths();
    const QString masks = m_project->masksDir();
    const Photo photo = *p;
    imaging::Segmenter *seg = segmenter();
    const bool useSmart = smart && smartAvailable();
    auto chain = m_chain;
    const int generation = m_selectionGeneration;
    ++m_strokesPending;
    emit selectionChanged();
    QPointer<EditorController> self(this);
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
                if (generation != self->m_selectionGeneration)
                    return;
                --self->m_strokesPending;
                self->m_selectionMask = key;
                emit self->selectionChanged();
            },
            Qt::QueuedConnection);
    });
}

void EditorController::clearSelection()
{
    {
        QMutexLocker locker(&m_chain->lock);
        m_chain->mask.clear();
    }
    m_selectionMask.clear();
    emit selectionChanged();
}

void EditorController::applySelection()
{
    if (!m_selecting || m_strokesPending > 0)
        return;
    if (!m_selectionMask.isEmpty()) {
        if (m_selectionLayer.isEmpty()) {
            // Camada nova: nasce com os valores e o preset do restante.
            imaging::Layer layer;
            layer.id = randomLayerId();
            layer.mask = m_selectionMask;
            layer.values = m_draft.values;
            layer.preset = m_draft.preset;
            m_draft.layers.push_back(layer);
            m_activeLayer = layer.id;
        } else {
            for (auto &l : m_draft.layers) {
                if (l.id == m_selectionLayer)
                    l.mask = m_selectionMask;
            }
        }
        saveDraft(false, true);
    }
    cancelSelection();
}

void EditorController::cancelSelection()
{
    {
        QMutexLocker locker(&m_chain->lock);
        ++m_chain->generation;
    }
    m_selectionGeneration = m_chain->generation;
    m_selecting = false;
    m_selectionLayer.clear();
    m_selectionMask.clear();
    m_strokesPending = 0;
    emit selectionChanged();
}

// --------------------------------------------------------------------------- //
// Ações
// --------------------------------------------------------------------------- //

void EditorController::deleteCurrent()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    const QString name = p->fileName;
    bool ok = false;
    guarded([&]() {
        m_project->deletePhoto(*p);
        ok = true;
    });
    if (!ok)
        return;
    setStatus(QStringLiteral("status.deleted"), {{QStringLiteral("name"), name}});
    reload();
}

void EditorController::duplicateCurrent()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    guarded([&]() {
        const Photo copy = m_project->duplicate(*p);
        setStatus(QStringLiteral("status.duplicated"), {{QStringLiteral("name"), copy.fileName}});
        reload(copy.id);
    });
}

void EditorController::toggleCover()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    guarded([&]() {
        const bool on = !p->isCover;
        const QString name = p->fileName;
        m_project->setCover(*p, on);
        setStatus(on ? QStringLiteral("status.coverSet") : QStringLiteral("status.coverCleared"),
                  {{QStringLiteral("name"), name}});
        reload();
    });
}

void EditorController::toggleInstagram()
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    if (p->isInstagram()) {
        selectPhoto(p->instagramOf);
        return;
    }
    guarded([&]() {
        const auto [version, created] = m_project->createInstagramVersion(*p);
        if (created)
            setStatus(QStringLiteral("status.instagramCreated"), {{QStringLiteral("name"), version.fileName}});
        reload(version.id);
    });
}

void EditorController::undo()
{
    guarded([&]() {
        const auto entry = m_project->undoLast();
        if (!entry) {
            setStatus(QStringLiteral("status.nothingToUndo"));
            return;
        }
        auto photo = m_project->photo(entry->photoId);
        setStatus(QStringLiteral("status.undone"),
                  {{QStringLiteral("kind"), entry->kind},
                   {QStringLiteral("name"), photo ? photo->fileName : QString()}});
        // Desfazer abre a foto da ação (de qualquer lugar da filmstrip). Se ela
        // saiu da filmstrip (cópia ou versão Instagram desfeita), abre a origem.
        QString focus = entry->photoId;
        if (photo && !photo->isActive())
            focus = photo->isInstagram() ? photo->instagramOf : (photo->isCopy() ? photo->copyOf : focus);
        reload(focus);
    });
}

void EditorController::redo()
{
    guarded([&]() {
        const auto entry = m_project->redoLast();
        if (!entry) {
            setStatus(QStringLiteral("status.nothingToRedo"));
            return;
        }
        auto photo = m_project->photo(entry->photoId);
        setStatus(QStringLiteral("status.redone"),
                  {{QStringLiteral("kind"), entry->kind},
                   {QStringLiteral("name"), photo ? photo->fileName : QString()}});
        // Refazer uma exclusão tira a foto da filmstrip: fica a vizinha.
        reload(photo && photo->isActive() ? entry->photoId : QString());
    });
    emit historyChanged();
}

bool EditorController::canUndo() const
{
    try {
        return m_project && m_project->nextUndo().has_value();
    } catch (const std::exception &) {
        return false;
    }
}

bool EditorController::canRedo() const
{
    try {
        return m_project && m_project->nextRedo().has_value();
    } catch (const std::exception &) {
        return false;
    }
}

QString EditorController::undoKind() const
{
    try {
        const auto e = m_project ? m_project->nextUndo() : std::nullopt;
        return e ? e->kind : QString();
    } catch (const std::exception &) {
        return {};
    }
}

QString EditorController::redoKind() const
{
    try {
        const auto e = m_project ? m_project->nextRedo() : std::nullopt;
        return e ? e->kind : QString();
    } catch (const std::exception &) {
        return {};
    }
}

void EditorController::rescan()
{
    guarded([&]() {
        const ScanSummary s = m_project->scan();
        setStatus(QStringLiteral("status.rescanned"), {{QStringLiteral("added"), s.added},
                                                        {QStringLiteral("updated"), s.updated},
                                                        {QStringLiteral("restored"), s.restored},
                                                        {QStringLiteral("missing"), s.missing},
                                                        {QStringLiteral("errors"), s.errors},
                                                        {QStringLiteral("total"), s.total}});
        reload();
    });
}

void EditorController::startExport()
{
    if (!m_exporter)
        return;
    commit();
    guarded([&]() { m_exporter->start(); });
}

QVariantMap EditorController::exportStatus() const
{
    if (!m_exporter || !m_project)
        return {};
    const ExportStatus s = m_exporter->status();
    return {{QStringLiteral("running"), s.running},
            {QStringLiteral("total"), s.total},
            {QStringLiteral("done"), s.done},
            {QStringLiteral("written"), s.written},
            {QStringLiteral("skipped"), s.skipped},
            {QStringLiteral("removed"), s.removed},
            {QStringLiteral("errors"), s.errors},
            {QStringLiteral("cover"), s.cover},
            {QStringLiteral("instagram"), s.instagram},
            {QStringLiteral("outputDir"), QStringLiteral("publicar")},
            {QStringLiteral("instagramDir"), QStringLiteral("publicar/instagram")}};
}

QVariantList EditorController::history() const
{
    const Photo *p = currentPhoto();
    if (!p || !m_project)
        return {};
    QVariantList out;
    try {
        for (const auto &e : m_project->history(*p)) {
            out << QVariantMap{{QStringLiteral("kind"), e.kind},
                               {QStringLiteral("created"), e.created.toLocalTime()},
                               {QStringLiteral("undone"), e.undoneAt.isValid()}};
        }
    } catch (const std::exception &) {
    }
    return out;
}

bool EditorController::hasInstagramVersions() const
{
    return std::any_of(m_model.photos().begin(), m_model.photos().end(), [](const Photo &p) { return p.isInstagram(); });
}

// --------------------------------------------------------------------------- //
// Merge
// --------------------------------------------------------------------------- //

void EditorController::setPicking(bool on)
{
    if (on == m_picking)
        return;
    m_picking = on;
    m_picked.clear();
    // Entrar no modo já marca a foto atual como base.
    if (on && currentPhoto() && !currentPhoto()->isInstagram())
        m_picked << currentPhoto()->id;
    m_model.setPicked(m_picked);
    emit pickingChanged();
}

void EditorController::togglePick(const QString &id)
{
    const QString n = Project::normalizeId(id);
    if (m_picked.contains(n))
        m_picked.removeAll(n);
    else if (m_picked.size() <= imaging::kMergeMaxSources)
        m_picked << n;
    // A base é a primeira NA ORDEM DA FILMSTRIP (como antes), não a primeira clicada.
    std::sort(m_picked.begin(), m_picked.end(),
              [this](const QString &a, const QString &b) { return m_model.indexOf(a) < m_model.indexOf(b); });
    m_model.setPicked(m_picked);
    emit pickingChanged();
}

void EditorController::createMerge()
{
    if (!m_project || m_picked.size() < 2 || m_busy)
        return;
    std::vector<Photo> ordered;
    guarded([&]() { ordered = m_project->mergeCandidates(m_picked); });
    if (ordered.size() < 2)
        return;
    setBusy(true);
    const ProjectPaths paths = m_project->paths();
    QPointer<EditorController> self(this);
    // O alinhamento (SIFT) de cada foto leva ~1 s: fora da interface.
    auto future = QtConcurrent::run([paths, ordered]() {
        std::vector<QJsonObject> aligns;
        for (size_t i = 1; i < ordered.size(); ++i)
            aligns.push_back(merges::analyze(paths, ordered.front(), ordered[i]));
        return aligns;
    });
    auto *watcher = new QFutureWatcher<std::vector<QJsonObject>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, ordered]() {
        watcher->deleteLater();
        setBusy(false);
        guarded([&]() {
            const MergeRecord merge = m_project->createMerge(ordered, watcher->result());
            setPicking(false);
            reload(merge.base);
            emit mergeCreated(merge.id);
        });
    });
    watcher->setFuture(future);
}

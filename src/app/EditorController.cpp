// SPDX-License-Identifier: BSD-2-Clause
#include "EditorController.h"

#include "ImageProviders.h"
#include "SelectionChain.h"

#include "core/Derivatives.h"
#include "imaging/Exif.h"
#include "imaging/ImageIO.h"
#include "imaging/Look.h"
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

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcEditor, "photoeditor.editor")

using namespace pe;


namespace {

constexpr int kRenderDebounceMs = 120;
/// Lado maior da imagem do histograma e dos níveis automáticos.
constexpr int kAnalysisSide = 512;
/// Níveis automáticos: corta 0,1% em cada ponta.
constexpr double kAutoLevelsClip = 0.001;
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

/// Reduz para caber em ``side`` px no lado maior (INTER_AREA).
cv::Mat fitSide(const cv::Mat &rgb, int side)
{
    const int longSide = std::max(rgb.cols, rgb.rows);
    if (longSide <= side)
        return rgb;
    const double s = static_cast<double>(side) / longSide;
    cv::Mat out;
    cv::resize(rgb, out, cv::Size(std::max(1, static_cast<int>(std::lround(rgb.cols * s))),
                                  std::max(1, static_cast<int>(std::lround(rgb.rows * s)))),
               0, 0, cv::INTER_AREA);
    return out;
}

/// Histograma do preview editado, no formato da propriedade ``histogram``.
QVariantMap histogramOf(const cv::Mat &rgb)
{
    std::array<std::array<double, 256>, 4> bins{};
    qint64 lowClip = 0, highClip = 0;
    for (int r = 0; r < rgb.rows; ++r) {
        const auto *row = rgb.ptr<cv::Vec3b>(r);
        for (int c = 0; c < rgb.cols; ++c) {
            const cv::Vec3b px = row[c];
            for (int k = 0; k < 3; ++k)
                bins[k][px[k]] += 1;
            const int l = static_cast<int>(0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2] + 0.5);
            bins[3][std::min(l, 255)] += 1;
            if (px[0] == 0 || px[1] == 0 || px[2] == 0)
                ++lowClip;
            if (px[0] == 255 || px[1] == 255 || px[2] == 255)
                ++highClip;
        }
    }
    // Raiz quadrada: o pico de um céu liso não achata todo o resto. O maior
    // valor fora dos extremos dá a escala (o estouro fica em 1).
    double peak = 0.0;
    for (const auto &b : bins) {
        for (int i = 1; i < 255; ++i)
            peak = std::max(peak, std::sqrt(b[i]));
    }
    if (peak <= 0.0)
        peak = 1.0;
    QVariantMap out;
    const char *names[] = {"r", "g", "b", "l"};
    for (int k = 0; k < 4; ++k) {
        QVariantList list;
        list.reserve(256);
        for (int i = 0; i < 256; ++i)
            list << std::min(1.0, std::sqrt(bins[k][i]) / peak);
        out.insert(QLatin1String(names[k]), list);
    }
    const double total = std::max<qint64>(1, static_cast<qint64>(rgb.total()));
    out.insert(QStringLiteral("clipShadows"), lowClip / total);
    out.insert(QStringLiteral("clipHighlights"), highClip / total);
    return out;
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
    m_histogramPool.setMaxThreadCount(1);
    connect(&m_renderTimer, &QTimer::timeout, this, &EditorController::updateEditedUrl);
}

EditorController::~EditorController()
{
    m_histogramLatest->fetch_add(1);
    m_histogramPool.waitForDone();
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
    m_view = QStringLiteral("photos");
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
    std::vector<Photo> photos = m_view == QLatin1String("archived") ? m_project->archivedPhotos()
        : m_view == QLatin1String("trash")                         ? m_project->trashedPhotos()
                                                                   : m_project->visiblePhotos();
    sortPhotos(photos, m_sortMode);
    m_archivedCount = m_project->countWithStatus(PhotoStatus::Archived);
    m_trashCount = m_project->countWithStatus(PhotoStatus::Deleted);
    emit countsChanged();
    QSet<QString> bases, withInstagram;
    for (const auto &m : m_project->liveMerges())
        bases.insert(m.base);
    for (const auto &p : photos) {
        if (p.isInstagram())
            withInstagram.insert(p.instagramOf);
    }
    // Antes do reset: os delegates da faixa pedem os thumbnails assim que o
    // modelo muda, e um pedido que chega antes do registro vira "image unavailable".
    registerThumbnails(photos);
    m_model.reset(std::move(photos), bases, withInstagram);
    m_model.setPicked(m_picked);
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

void EditorController::registerThumbnails(const std::vector<Photo> &photos)
{
    const ProjectPaths paths = m_project->paths();
    for (const Photo &p : photos) {
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
    // Na filmstrip as versões Instagram ficam escondidas (abrem pelo I); no
    // arquivo e na lixeira elas são itens como qualquer outro.
    for (int i = m_index + 1; i < m_model.count(); ++i) {
        if (!editable() || !m_model.at(i).isInstagram()) {
            setCurrentIndex(i);
            return;
        }
    }
}

void EditorController::previous()
{
    for (int i = m_index - 1; i >= 0; --i) {
        if (!editable() || !m_model.at(i).isInstagram()) {
            setCurrentIndex(i);
            return;
        }
    }
}

void EditorController::setView(const QString &view)
{
    const QString v = (view == QLatin1String("archived") || view == QLatin1String("trash")) ? view : QStringLiteral("photos");
    if (v == m_view)
        return;
    commit();
    if (m_selecting)
        cancelSelection();
    setPicking(false);
    m_view = v;
    m_index = 0;
    m_activeLayer.clear();
    emit viewChanged();
    reload(QStringLiteral("-"));  // nenhuma foto preferida: começa da primeira
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
    loadShootingInfo();
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
    if (!editable()) {
        // Arquivo e lixeira: todos os itens contam, versões Instagram inclusive.
        position = m_index + 1;
        total = m_model.count();
    }
    for (int i = 0; editable() && i < m_model.count(); ++i) {
        const Photo &q = m_model.at(i);
        if (q.isInstagram())
            continue;
        ++total;
        if (i <= m_index || q.id == p->instagramOf)
            position = total;
    }
    if (editable() && p->isInstagram()) {
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
        {QStringLiteral("status"), p->status},
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
        scheduleHistogram();
        return;
    }
    // A chave carrega tudo de que o render depende: a URL só muda quando o
    // resultado muda, e o QML não pede de novo o que já tem.
    QString hash = m_draft.settingsHash();
    for (const auto &l : m_draft.layers)
        hash += u'.' + l.mask.left(6);
    const QString key = QStringLiteral("%1-%2-%3-%4").arg(p->id).arg(p->mtimeNs).arg(hash, m_mergeVersion);
    const RenderRequest req = renderRequest(m_draft);
    ImageRegistry::instance().put(QStringLiteral("render"), key,
                                  [req]() { return imagefiles::load(pe::render(req)); });
    const QString url = QStringLiteral("image://photo/render/") + key;
    if (url != m_editedUrl) {
        m_editedUrl = url;
        emit editedUrlChanged();
    }
    scheduleHistogram();
}

RenderRequest EditorController::renderRequest(const EditState &state) const
{
    return RenderRequest{m_project->paths(), *currentPhoto(), state, m_merge, m_mergeVersion};
}

void EditorController::scheduleHistogram()
{
    const int generation = m_histogramLatest->fetch_add(1) + 1;
    const Photo *p = currentPhoto();
    if (!p || !m_project) {
        if (!m_histogram.isEmpty()) {
            m_histogram.clear();
            emit histogramChanged();
        }
        return;
    }
    // Montado aqui (thread principal); o trabalho só lê arquivos.
    const RenderRequest req = renderRequest(m_draft);
    const auto latest = m_histogramLatest;
    QPointer<EditorController> self(this);
    m_histogramPool.start([req, latest, generation, self]() {
        if (latest->load() != generation)
            return;
        const cv::Mat rgb = imaging::loadRgb(editSource(req).first);
        if (rgb.empty())
            return;
        const cv::Mat out = developImage(
            rgb, req.state, req.paths.masksDir(), [](const cv::Mat &m) { return fitSide(m, kAnalysisSide); },
            imaging::grainSeed(req.photo.id));
        QVariantMap h = histogramOf(out);
        QMetaObject::invokeMethod(
            self.data(),
            [self, latest, generation, h]() {
                if (!self || latest->load() != generation)
                    return;
                self->m_histogram = h;
                emit self->histogramChanged();
            },
            Qt::QueuedConnection);
    });
}

void EditorController::loadShootingInfo()
{
    m_shootingInfo.clear();
    const Photo *p = currentPhoto();
    if (!p || !m_project)
        return;
    const auto header = imaging::readJpegHeader(m_project->rawPathOf(*p));
    if (!header)
        return;
    const imaging::ExifInfo exif = imaging::parseExif(header->exif);
    if (exif.iso > 0)
        m_shootingInfo.insert(QStringLiteral("iso"), exif.iso);
    if (exif.focalLength > 0)
        m_shootingInfo.insert(QStringLiteral("focalLength"), std::nearbyint(exif.focalLength * 10.0) / 10.0);
    if (exif.fNumber > 0)
        m_shootingInfo.insert(QStringLiteral("aperture"), imaging::formatAperture(exif.fNumber));
    if (exif.exposureTime > 0)
        m_shootingInfo.insert(QStringLiteral("shutter"), imaging::formatShutter(exif.exposureTime));
    const QString camera = imaging::cameraName(exif.make, exif.model);
    if (!camera.isEmpty())
        m_shootingInfo.insert(QStringLiteral("camera"), camera);
    if (!exif.lensModel.simplified().isEmpty())
        m_shootingInfo.insert(QStringLiteral("lens"), exif.lensModel.simplified());
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

imaging::Look &EditorController::draftLook()
{
    for (auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            return l.look;
    }
    return m_draft.look;
}

const imaging::Look &EditorController::activeLook() const
{
    for (const auto &l : m_draft.layers) {
        if (l.id == m_activeLayer)
            return l.look;
    }
    return m_draft.look;
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
    if (!slider || !currentPhoto() || !editable())
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
                                                       : std::nullopt,
                                            m_draft.look);
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

void EditorController::saveAs(const QString &kind)
{
    const Photo *p = currentPhoto();
    if (!p)
        return;
    m_renderTimer.stop();
    guarded([&]() {
        EditState next = m_draft;
        next.crop = m_saved.crop;
        m_saved = m_project->applyState(*p, next, kind);
    });
    const imaging::Crop keepCrop = m_draft.crop;
    m_draft = m_saved;
    m_draft.crop = keepCrop;
    updateEditedUrl();
    emit stateChanged();
    emit historyChanged();
}

void EditorController::applyPreset(const QString &id)
{
    if (!currentPhoto() || !editable() || !imaging::isPreset(id))
        return;
    commit();
    imaging::Values &v = draftValues();
    // effective() = valores + deslocamentos, cada um no limite do slider.
    v = imaging::effective(v, id);
    saveAs(HistoryKind::Preset);
}

void EditorController::pickWhite(double x, double y)
{
    const Photo *p = currentPhoto();
    if (!p || !m_project || !editable() || !std::isfinite(x) || !std::isfinite(y))
        return;
    commit();
    // A etapa 1 do motor (balanço de branco) recebe o original: é nele que a
    // amostra tem de ficar neutra (o resto do motor preserva o cinza).
    const cv::Mat rgb = imaging::loadRgb(editSource(renderRequest(m_draft)).first);
    if (rgb.empty())
        return;
    const int side = std::max(5, static_cast<int>(std::lround(0.01 * std::max(rgb.cols, rgb.rows))));
    const int cx = static_cast<int>(std::clamp(x, 0.0, 1.0) * (rgb.cols - 1));
    const int cy = static_cast<int>(std::clamp(y, 0.0, 1.0) * (rgb.rows - 1));
    const cv::Rect box = cv::Rect(cx - side / 2, cy - side / 2, side, side) & cv::Rect(0, 0, rgb.cols, rgb.rows);
    if (box.empty())
        return;
    const cv::Scalar mean = cv::mean(rgb(box));
    const double lo = std::min({mean[0], mean[1], mean[2]});
    const double hi = std::max({mean[0], mean[1], mean[2]});
    if (lo < 10.0 || hi > 250.0) {
        emit failed(QStringLiteral("wb.badSample"), {});
        return;
    }
    const double gray = (mean[0] + mean[1] + mean[2]) / 3.0;
    const auto [temperature, tint] = imaging::gainsToWb(cv::Vec3d(gray / mean[0], gray / mean[1], gray / mean[2]));
    // O preset legado soma o dele por cima: desconta para o efetivo ser o medido.
    const imaging::Values offsets = imaging::effective(imaging::kNeutral, draftPreset());
    imaging::Values &v = draftValues();
    v[imaging::Temperature] = imaging::clampValue(imaging::Temperature, temperature - offsets[imaging::Temperature]);
    v[imaging::Tint] = imaging::clampValue(imaging::Tint, tint - offsets[imaging::Tint]);
    saveAs(HistoryKind::Adjust);
}

// --------------------------------------------------------------------------- //
// Níveis, curvas e efeitos
// --------------------------------------------------------------------------- //

QVariantMap EditorController::levels() const
{
    const imaging::Look &look = activeLook();
    QVariantMap out;
    for (int c = 0; c < imaging::ChannelCount; ++c) {
        out.insert(QLatin1String(imaging::channelName(static_cast<imaging::Channel>(c))),
                   imaging::levelsChannelToJson(look.levels[c]).toVariantMap());
    }
    return out;
}

QVariantMap EditorController::curves() const
{
    const imaging::Look &look = activeLook();
    QVariantMap out;
    for (int c = 0; c < imaging::ChannelCount; ++c) {
        out.insert(QLatin1String(imaging::channelName(static_cast<imaging::Channel>(c))),
                   imaging::curveToJson(look.curves[c]).toVariantList());
    }
    return out;
}

QVariantList EditorController::effects() const
{
    return imaging::effectsToJson(activeLook().effects).toVariantList();
}

QVariantList EditorController::effectCatalog() const
{
    QVariantList out;
    for (const auto &e : imaging::effectCatalog()) {
        out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(e.id)},
                           {QStringLiteral("defaultAmount"), e.defaultAmount}};
    }
    return out;
}

void EditorController::setLevels(const QString &channel, const QVariantMap &levels)
{
    const auto ch = imaging::channelByName(channel);
    if (!ch || !currentPhoto() || !editable())
        return;
    imaging::LevelsChannel &l = draftLook().levels[*ch];
    auto num = [&](const char *key, double current) {
        bool ok = false;
        const double v = levels.value(QLatin1String(key)).toDouble(&ok);
        return ok && std::isfinite(v) ? v : current;
    };
    imaging::LevelsChannel next;
    next.inBlack = static_cast<int>(std::lround(std::clamp(num("inBlack", l.inBlack), -1.0, 256.0)));
    next.gamma = num("gamma", l.gamma);
    next.inWhite = static_cast<int>(std::lround(std::clamp(num("inWhite", l.inWhite), -1.0, 256.0)));
    next.outBlack = static_cast<int>(std::lround(std::clamp(num("outBlack", l.outBlack), -1.0, 256.0)));
    next.outWhite = static_cast<int>(std::lround(std::clamp(num("outWhite", l.outWhite), -1.0, 256.0)));
    l = imaging::normalizeLevelsChannel(next);
    emit stateChanged();
    scheduleRender();
}

void EditorController::autoLevels(const QString &channel)
{
    const auto ch = imaging::channelByName(channel);
    const Photo *p = currentPhoto();
    if (!ch || !p || !m_project || !editable())
        return;
    commit();
    // Mede o que CHEGA aos níveis deste canal: a camada ativa sozinha, sem os
    // níveis do canal, sem curvas e sem efeitos (que vêm depois).
    EditState probe = m_draft;
    imaging::Look look = activeLook();
    look.levels[*ch] = imaging::kLevelsIdentity;
    look.curves = {};
    look.effects.clear();
    QString mask;
    for (const auto &l : m_draft.layers) {
        if (l.id == m_activeLayer) {
            probe.values = l.values;
            probe.preset = l.preset;
            mask = l.mask;
        }
    }
    probe.look = look;
    probe.layers.clear();
    const RenderRequest req = renderRequest(probe);
    const cv::Mat rgb = imaging::loadRgb(editSource(req).first);
    if (rgb.empty())
        return;
    const cv::Mat out = developImage(
        rgb, probe, req.paths.masksDir(), [](const cv::Mat &m) { return fitSide(m, kAnalysisSide); },
        imaging::grainSeed(p->id));
    // Numa camada, só os pixels da área dela contam.
    cv::Mat alpha;
    if (!mask.isEmpty())
        alpha = imaging::layerAlpha(m_project->masksDir(), mask, rgb.size(), probe.crop, out.size());
    std::array<double, 256> hist{};
    double total = 0.0;
    for (int r = 0; r < out.rows; ++r) {
        for (int c = 0; c < out.cols; ++c) {
            if (!alpha.empty() && alpha.at<float>(r, c) < 0.5f)
                continue;
            const cv::Vec3b px = out.at<cv::Vec3b>(r, c);
            const int v = *ch == imaging::ChannelRgb
                ? std::min(255, static_cast<int>(0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2] + 0.5))
                : px[*ch - 1];
            hist[v] += 1;
            total += 1;
        }
    }
    if (total <= 0)
        return;
    int lo = 0, hi = 255;
    for (double acc = 0; lo < 255; ++lo) {
        acc += hist[lo];
        if (acc > total * kAutoLevelsClip)
            break;
    }
    for (double acc = 0; hi > 0; --hi) {
        acc += hist[hi];
        if (acc > total * kAutoLevelsClip)
            break;
    }
    imaging::LevelsChannel &l = draftLook().levels[*ch];
    imaging::LevelsChannel next = l;
    next.inBlack = std::min(lo, 253);
    next.inWhite = std::max(hi, next.inBlack + 2);
    l = imaging::normalizeLevelsChannel(next);
    saveAs(HistoryKind::Levels);
}

void EditorController::resetLevels(const QString &channel)
{
    const auto ch = imaging::channelByName(channel);
    if (!ch || !currentPhoto() || !editable())
        return;
    draftLook().levels[*ch] = imaging::kLevelsIdentity;
    saveDraft(false, true);
}

void EditorController::setCurve(const QString &channel, const QVariantList &points)
{
    const auto ch = imaging::channelByName(channel);
    if (!ch || !currentPhoto() || !editable())
        return;
    imaging::Curve curve;
    for (const QVariant &v : points) {
        if (v.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap m = v.toMap();
            curve.push_back({m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()});
            continue;
        }
        const QVariantList xy = v.toList();
        if (xy.size() == 2)
            curve.push_back({xy[0].toDouble(), xy[1].toDouble()});
    }
    draftLook().curves[*ch] = imaging::normalizeCurve(curve);
    emit stateChanged();
    scheduleRender();
}

void EditorController::resetCurve(const QString &channel)
{
    const auto ch = imaging::channelByName(channel);
    if (!ch || !currentPhoto() || !editable())
        return;
    draftLook().curves[*ch].clear();
    saveDraft(false, true);
}

void EditorController::setEffect(const QString &id, int amount)
{
    if (!imaging::isEffect(id) || !currentPhoto() || !editable())
        return;
    imaging::Effects &effects = draftLook().effects;
    std::erase_if(effects, [&](const imaging::Effect &e) { return e.id == id; });
    effects.push_back({id, std::clamp(amount, 0, 100)});
    effects = imaging::normalizeEffects(effects);
    emit stateChanged();
    scheduleRender();
}

void EditorController::toggleEffect(const QString &id)
{
    if (!imaging::isEffect(id) || !currentPhoto() || !editable())
        return;
    imaging::Effects &effects = draftLook().effects;
    if (imaging::effectAmount(effects, id) > 0) {
        std::erase_if(effects, [&](const imaging::Effect &e) { return e.id == id; });
    } else {
        for (const auto &spec : imaging::effectCatalog()) {
            if (id == QLatin1String(spec.id))
                effects.push_back({id, spec.defaultAmount});
        }
        effects = imaging::normalizeEffects(effects);
    }
    saveDraft(false, true);
}

void EditorController::togglePreset(const QString &id)
{
    if (!currentPhoto() || !editable())
        return;
    QString &preset = draftPreset();
    preset = preset == id ? QString() : imaging::normalizePreset(id);
    saveDraft(false, true);
}

void EditorController::autoAdjust()
{
    const Photo *p = currentPhoto();
    if (!p || !editable())
        return;
    setBusy(true);
    guarded([&]() { m_project->runAuto(*p, m_activeLayer); });
    setBusy(false);
    loadCurrent();
}

void EditorController::reset()
{
    const Photo *p = currentPhoto();
    if (!p || !editable())
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
    if (!p || !editable())
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
    if (!p || !m_project || !editable())
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
            layer.look = m_draft.look;
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
    if (!p || m_view == QLatin1String("trash"))
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

void EditorController::archiveCurrent()
{
    const Photo *p = currentPhoto();
    if (!p || !editable())
        return;
    const QString name = p->fileName;
    bool ok = false;
    guarded([&]() {
        m_project->archivePhoto(*p);
        ok = true;
    });
    if (!ok)
        return;
    setStatus(QStringLiteral("status.archived"), {{QStringLiteral("name"), name}});
    reload();
}

void EditorController::restoreCurrent()
{
    const Photo *p = currentPhoto();
    if (!p || editable())
        return;
    const QString name = p->fileName;
    bool ok = false;
    guarded([&]() {
        m_project->restorePhoto(*p);
        ok = true;
    });
    if (!ok)
        return;
    setStatus(QStringLiteral("status.restored"), {{QStringLiteral("name"), name}});
    reload();
}

void EditorController::purgeCurrent()
{
    const Photo *p = currentPhoto();
    if (!p || m_view != QLatin1String("trash"))
        return;
    const QString name = p->fileName;
    guarded([&]() { m_project->purge(*p); });
    setStatus(QStringLiteral("status.purged"), {{QStringLiteral("name"), name}});
    reload();
    emit historyChanged();
}

void EditorController::emptyTrash()
{
    if (!m_project)
        return;
    int count = 0;
    guarded([&]() { count = m_project->emptyTrash(); });
    setStatus(QStringLiteral("status.trashEmptied"), {{QStringLiteral("count"), count}});
    reload();
    emit historyChanged();
}

void EditorController::duplicateCurrent()
{
    const Photo *p = currentPhoto();
    if (!p || !editable())
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
    if (!p || !editable())
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
    if (!p || !editable())
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

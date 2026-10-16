// SPDX-License-Identifier: BSD-2-Clause
#include "core/Merges.h"

#include "core/Derivatives.h"
#include "imaging/ImageIO.h"
#include "imaging/Mask.h"
#include "imaging/Merge.h"
#include "imaging/SettingsHash.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSqlQuery>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

Q_LOGGING_CATEGORY(lcMerge, "photoeditor.merge")

namespace pe {

namespace {

cv::Size fullSize(const Photo &p)
{
    return {p.width, p.height};
}

cv::Size sizeOf(const cv::Mat &m)
{
    return {m.cols, m.rows};
}

cv::Mat previewRgb(const ProjectPaths &paths, const Photo &p)
{
    return imaging::loadRgb(ensurePreview(paths, p));
}

cv::Matx23d alignMatrix(const QJsonObject &align)
{
    return imaging::mergeAlignFromJson(align).matrix;
}

cv::Vec3d alignGainOf(const QJsonObject &align)
{
    return imaging::mergeAlignFromJson(align).gain;
}

bool usable(const MergeLayer &e, const MergeSnapshot &s)
{
    const auto it = s.photos.find(e.photo);
    return it != s.photos.end() && it->isActive() && e.align.value(QStringLiteral("ok")).toBool();
}

QString merged(const QStringList &parts)
{
    return imaging::sha1Hex(parts.join(u'|').toUtf8());
}

QJsonArray layersToJson(const std::vector<MergeLayer> &layers)
{
    QJsonArray out;
    for (const auto &e : layers) {
        out.append(QJsonObject{{QStringLiteral("photo"), Project::dashedId(e.photo)},
                               {QStringLiteral("mask"), e.mask},
                               {QStringLiteral("opacity"), e.opacity},
                               {QStringLiteral("visible"), e.visible},
                               {QStringLiteral("align"), e.align}});
    }
    return out;
}

double clamp01(const QJsonValue &v, double fallback)
{
    if (!v.isDouble())
        return fallback;
    return std::clamp(v.toDouble(), 0.0, 1.0);
}

double round3(double v)
{
    return std::nearbyint(v * 1000.0) / 1000.0;
}

QByteArray encodePng(const cv::Mat &rgba)
{
    cv::Mat bgra;
    cv::cvtColor(rgba, bgra, cv::COLOR_RGBA2BGRA);
    std::vector<uchar> buf;
    cv::imencode(".png", bgra, buf);
    return QByteArray(reinterpret_cast<const char *>(buf.data()), static_cast<qsizetype>(buf.size()));
}

std::optional<imaging::MergePiece> previewPiece(const ProjectPaths &paths, const Photo &base, const Photo &src,
                                                const MergeLayer &entry, const cv::Mat &mask, cv::Size outSize)
{
    // Área da camada (máscara ``mask``) levada ao preview da base.
    const cv::Mat srcRgb = previewRgb(paths, src);
    if (mask.size() != srcRgb.size())
        return std::nullopt;
    const cv::Matx23d matrix
        = imaging::scaledMatrix(alignMatrix(entry.align), fullSize(src), sizeOf(srcRgb), fullSize(base), outSize);
    cv::Mat alpha;
    mask.convertTo(alpha, CV_32F, 1.0 / 255.0);
    return imaging::cutout(srcRgb, alpha, matrix, alignGainOf(entry.align), outSize);
}

std::optional<imaging::MergePiece> previewBackdrop(const ProjectPaths &paths, const Photo &base, const Photo &src,
                                                   const MergeLayer &entry, const cv::Mat &baseMask)
{
    // Fundo atrás do objeto da base (máscara ``baseMask``), no preview.
    const auto region = imaging::maskRegion(baseMask, sizeOf(baseMask));
    if (!region)
        return std::nullopt;
    const cv::Mat srcRgb = previewRgb(paths, src);
    const cv::Matx23d matrix
        = imaging::scaledMatrix(alignMatrix(entry.align), fullSize(src), sizeOf(srcRgb), fullSize(base), sizeOf(baseMask));
    return imaging::backdrop(srcRgb, region->first, region->second, matrix, alignGainOf(entry.align));
}

} // namespace

// --------------------------------------------------------------------------- //
// Funções puras (qualquer thread)
// --------------------------------------------------------------------------- //

namespace merges {

std::vector<MergeLayer> readyLayers(const MergeRecord &merge)
{
    std::vector<MergeLayer> out;
    for (const auto &e : merge.layers) {
        if (e.visible && !e.mask.isEmpty())
            out.push_back(e);
    }
    return out;
}

std::optional<MergeLayer> backdropSource(const MergeSnapshot &s, const std::optional<QString> &maskKey)
{
    // A camada mais recente (o objeto já andou mais) cuja área, levada à
    // base, não cai sobre a área da base. Sem área para comparar, a mais recente.
    const QString key = maskKey.value_or(s.merge.baseMask);
    const QString masks = s.paths.masksDir();
    const cv::Mat baseMask = key.isEmpty() ? cv::Mat() : imaging::loadMask(masks, key);
    std::vector<MergeLayer> candidates;
    for (auto it = s.merge.layers.rbegin(); it != s.merge.layers.rend(); ++it) {
        if (usable(*it, s))
            candidates.push_back(*it);
    }
    if (baseMask.empty() || candidates.empty() || !imaging::maskBounds(baseMask))
        return std::nullopt;
    const Photo &base = s.photos.value(s.merge.base);
    for (const auto &entry : candidates) {
        const cv::Mat mask = entry.mask.isEmpty() ? cv::Mat() : imaging::loadMask(masks, entry.mask);
        if (mask.empty() || !imaging::maskBounds(mask))
            continue;
        const Photo &src = s.photos.value(entry.photo);
        const cv::Matx23d m = imaging::scaledMatrix(alignMatrix(entry.align), fullSize(src), sizeOf(mask),
                                                    fullSize(base), sizeOf(baseMask));
        if (imaging::layerClearsBaseArea(baseMask, mask, m))
            return entry;
    }
    return candidates.front();
}

bool baseFades(const MergeSnapshot &s)
{
    if (s.merge.baseMask.isEmpty() || s.merge.baseOpacity >= 0.999)
        return false;
    return backdropSource(s).has_value();
}

QString version(const MergeSnapshot &s)
{
    const auto ready = readyLayers(s.merge);
    const bool fades = baseFades(s);
    if (ready.empty() && !fades)
        return {};
    QStringList parts{QString::number(s.photos.value(s.merge.base).mtimeNs)};
    QString backdrop;
    if (fades) {
        if (const auto entry = backdropSource(s)) {
            backdrop = s.merge.baseMask + u':' + imaging::pyFloat(s.merge.baseOpacity) + u':'
                + Project::dashedId(entry->photo) + u':' + QString::number(s.photos.value(entry->photo).mtimeNs) + u':'
                + imaging::alignVersion(entry->align);
        }
    }
    parts << backdrop;
    for (const auto &e : ready) {
        const auto it = s.photos.find(e.photo);
        parts << Project::dashedId(e.photo) + u':' + QString::number(it != s.photos.end() ? it->mtimeNs : 0) + u':'
                + e.mask + u':' + imaging::pyFloat(e.opacity) + u':' + imaging::alignVersion(e.align);
    }
    return merged(parts).left(12);
}

QString layersVersion(const MergeSnapshot &s)
{
    QStringList parts;
    for (const auto &e : s.merge.layers) {
        const auto it = s.photos.find(e.photo);
        // f"{...}:{e.get('mask')}" do Python: sem máscara sai "" (gravado como '').
        parts << Project::dashedId(e.photo) + u':' + QString::number(it != s.photos.end() ? it->mtimeNs : 0) + u':'
                + e.mask + u':' + imaging::alignVersion(e.align);
    }
    return merged(parts).left(10);
}

QJsonObject analyze(const ProjectPaths &paths, const Photo &base, const Photo &src, const QString &maskKey)
{
    const cv::Mat baseRgb = previewRgb(paths, base);
    const cv::Mat srcRgb = previewRgb(paths, src);
    cv::Mat mask = maskKey.isEmpty() ? cv::Mat() : imaging::loadMask(paths.masksDir(), maskKey);
    if (!mask.empty() && mask.size() != srcRgb.size())
        mask = cv::Mat();
    imaging::MergeAlign info = imaging::align(baseRgb, srcRgb, mask);
    // Preview -> resolução cheia, que é como a matriz fica guardada.
    info.matrix = imaging::scaledMatrix(info.matrix, sizeOf(srcRgb), fullSize(src), sizeOf(baseRgb), fullSize(base));
    QJsonObject out = imaging::mergeAlignToJson(info);
    out.insert(QStringLiteral("mask"), mask.empty() ? QString() : maskKey);
    const auto d = imaging::describe(info.matrix);
    qCInfo(lcMerge).nospace() << "Merge " << base.fileName << " <- " << src.fileName << ": scale=" << d.scale
                              << " angle=" << d.angle << " dx=" << d.dx << " dy=" << d.dy << " inliers=" << info.inliers;
    return out;
}

QString compositePreviewPath(const MergeSnapshot &s, const QString &ver)
{
    const Photo &base = s.photos.value(s.merge.base);
    const QString path = s.paths.cacheDir(QStringLiteral("merge-preview")) + u'/' + Project::dashedId(base.id) + u'-'
        + ver + QStringLiteral(".jpg");
    if (QFileInfo(path).isFile())
        return path;
    const QString masks = s.paths.masksDir();
    cv::Mat canvas = previewRgb(s.paths, base);
    const cv::Size outSize = sizeOf(canvas);
    if (baseFades(s)) {
        if (const auto entry = backdropSource(s)) {
            const auto piece = previewBackdrop(s.paths, base, s.photos.value(entry->photo), *entry,
                                               imaging::loadMask(masks, s.merge.baseMask));
            if (piece)
                imaging::paste(canvas, *piece, 1.0 - s.merge.baseOpacity);
        }
    }
    for (const auto &entry : readyLayers(s.merge)) {
        const auto it = s.photos.find(entry.photo);
        const cv::Mat mask = imaging::loadMask(masks, entry.mask);
        if (it == s.photos.end() || !it->isActive() || mask.empty())
            continue;
        if (const auto piece = previewPiece(s.paths, base, *it, entry, mask, outSize))
            imaging::paste(canvas, *piece, entry.opacity);
    }
    imaging::writeAtomic(path, imaging::encodeJpeg(canvas, imaging::kPreviewQuality));
    return path;
}

QByteArray cutoutPng(const MergeSnapshot &s, const QString &photoId, const QString &maskKey)
{
    const QString masks = s.paths.masksDir();
    const cv::Mat mask = maskKey.isEmpty() ? cv::Mat() : imaging::loadMask(masks, maskKey);
    if (mask.empty())
        return {};
    const Photo &base = s.photos.value(s.merge.base);
    const QString id = Project::normalizeId(photoId);
    std::optional<MergeLayer> entry;
    QString kind;
    if (id == base.id) {
        entry = backdropSource(s, maskKey);
        kind = QStringLiteral("bg");
    } else {
        for (const auto &e : s.merge.layers) {
            if (e.photo == id)
                entry = e;
        }
        kind = QStringLiteral("fg");
    }
    if (!entry || !s.photos.contains(entry->photo))
        return {};
    const Photo &src = s.photos.value(entry->photo);
    const QString path = s.paths.cacheDir(QStringLiteral("merge")) + u'/'
        + QStringLiteral("%1-%2-%3-%4-%5-%6-%7.png")
              .arg(Project::dashedId(s.merge.id), kind, Project::dashedId(src.id), QString::number(base.mtimeNs),
                   QString::number(src.mtimeNs), imaging::alignVersion(entry->align), maskKey);
    if (QFileInfo(path).isFile()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            return f.readAll();
    }
    const cv::Mat basePreview = previewRgb(s.paths, base);
    const cv::Size outSize = sizeOf(basePreview);
    const std::optional<imaging::MergePiece> piece = kind == QLatin1String("bg")
        ? previewBackdrop(s.paths, base, src, *entry, mask)
        : previewPiece(s.paths, base, src, *entry, mask, outSize);
    const QByteArray data = encodePng(imaging::pieceToRgba(piece, outSize));
    imaging::writeAtomic(path, data);
    return data;
}

std::pair<cv::Mat, QByteArray> renderFull(const MergeSnapshot &s)
{
    const Photo &base = s.photos.value(s.merge.base);
    const QString masks = s.paths.masksDir();
    const QString basePath = base.sourcePath.isEmpty() ? s.paths.rawPath(base.fileName) : base.sourcePath;
    const auto header = imaging::readJpegHeader(basePath);
    const QByteArray exif = header ? header->exif : QByteArray();
    cv::Mat canvas = imaging::loadRgb(basePath);
    const cv::Size size = sizeOf(canvas);
    auto rawOf = [&](const Photo &p) {
        return p.sourcePath.isEmpty() ? s.paths.rawPath(p.fileName) : p.sourcePath;
    };
    if (baseFades(s)) {
        if (const auto entry = backdropSource(s)) {
            const Photo &src = s.photos.value(entry->photo);
            const auto region = imaging::maskRegion(imaging::loadMask(masks, s.merge.baseMask), size);
            if (region) {
                const cv::Mat img = imaging::loadRgb(rawOf(src));
                const cv::Matx23d matrix
                    = imaging::scaledMatrix(alignMatrix(entry->align), fullSize(src), sizeOf(img), fullSize(base), size);
                imaging::paste(canvas,
                               imaging::backdrop(img, region->first, region->second, matrix, alignGainOf(entry->align)),
                               1.0 - s.merge.baseOpacity);
            }
        }
    }
    for (const auto &entry : readyLayers(s.merge)) {
        const auto it = s.photos.find(entry.photo);
        if (it == s.photos.end() || !it->isActive() || !QFileInfo(rawOf(*it)).isFile()) {
            qCWarning(lcMerge) << "Merge" << s.merge.id << ": layer photo" << entry.photo << "is unavailable; skipped.";
            continue;
        }
        const cv::Mat mask = imaging::loadMask(masks, entry.mask);
        if (mask.empty())
            continue;
        // A máscara vive no preview: só a caixa dela é ampliada, e só esse
        // pedaço da foto de origem entra na conta.
        const cv::Mat img = imaging::loadRgb(rawOf(*it));
        const auto region = imaging::maskRegion(mask, sizeOf(img));
        if (!region)
            continue;
        const cv::Rect box(region->second, region->first.size());
        const cv::Mat rgb = img(box & cv::Rect(0, 0, img.cols, img.rows));
        const cv::Matx23d matrix
            = imaging::scaledMatrix(alignMatrix(entry.align), fullSize(*it), sizeOf(img), fullSize(base), size);
        if (const auto piece = imaging::cutout(rgb, region->first(cv::Rect(0, 0, rgb.cols, rgb.rows)), matrix,
                                               alignGainOf(entry.align), size, region->second))
            imaging::paste(canvas, *piece, entry.opacity);
    }
    return {canvas, exif};
}

} // namespace merges

// --------------------------------------------------------------------------- //
// Project: criar, editar, desfazer (thread principal)
// --------------------------------------------------------------------------- //

MergeRecord Project::mergeFromQuery(const QSqlQuery &q) const
{
    MergeRecord m;
    m.id = q.value(0).toString();
    m.base = q.value(1).toString();
    m.baseMask = q.value(3).toString();
    m.baseOpacity = q.value(4).toDouble();
    for (const auto &v : QJsonDocument::fromJson(q.value(2).toString().toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        MergeLayer e;
        e.photo = normalizeId(o.value(QStringLiteral("photo")).toString());
        e.mask = o.value(QStringLiteral("mask")).toString();
        e.opacity = o.value(QStringLiteral("opacity")).toDouble(1.0);
        e.visible = o.value(QStringLiteral("visible")).toBool(true);
        e.align = o.value(QStringLiteral("align")).toObject();
        m.layers.push_back(e);
    }
    return m;
}

std::vector<MergeRecord> Project::liveMerges() const
{
    // Base excluída (em deleted/) = merge parado: as fotos das camadas voltam
    // a aparecer, e desfazer a exclusão traz tudo de volta.
    QSqlQuery q = m_db.exec(QStringLiteral(
        "SELECT m.id, m.base_id, m.layers, m.base_mask, m.base_opacity FROM photoeditor_merge m "
        "JOIN photoeditor_photo p ON p.id = m.base_id WHERE p.status = 'active' ORDER BY m.created"));
    std::vector<MergeRecord> out;
    while (q.next())
        out.push_back(mergeFromQuery(q));
    return out;
}

std::optional<MergeRecord> Project::merge(const QString &id) const
{
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT id, base_id, layers, base_mask, base_opacity FROM photoeditor_merge "
                                           "WHERE id = ?"),
                            {normalizeId(id)});
    if (!q.next())
        return std::nullopt;
    return mergeFromQuery(q);
}

std::optional<MergeRecord> Project::mergeForBase(const QString &photoId) const
{
    const QString id = normalizeId(photoId);
    for (auto &m : liveMerges()) {
        if (m.base == id)
            return m;
    }
    return std::nullopt;
}

std::optional<MergeRecord> Project::mergeForPhoto(const Photo &p) const
{
    return mergeForBase(p.isInstagram() ? p.instagramOf : p.id);
}

QSet<QString> Project::mergeHiddenIds() const
{
    QSet<QString> ids;
    for (const auto &m : liveMerges()) {
        for (const auto &e : m.layers)
            ids.insert(e.photo);
    }
    if (ids.isEmpty())
        return ids;
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT id, instagram_of_id FROM photoeditor_photo WHERE instagram_of_id IS NOT NULL"));
    while (q.next()) {
        if (ids.contains(q.value(1).toString()))
            ids.insert(q.value(0).toString());
    }
    return ids;
}

MergeSnapshot Project::mergeSnapshot(const MergeRecord &merge) const
{
    MergeSnapshot s{merge, {}, m_paths};
    if (auto base = photo(merge.base))
        s.photos.insert(base->id, *base);
    for (const auto &e : merge.layers) {
        if (auto p = photo(e.photo))
            s.photos.insert(p->id, *p);
    }
    return s;
}

QString Project::mergeVersion(const std::optional<MergeRecord> &merge) const
{
    if (!merge)
        return {};
    return merges::version(mergeSnapshot(*merge));
}

QString Project::mergeExportName(const Photo &p)
{
    return imaging::mergeExportName(p.fileName);
}

std::vector<Photo> Project::mergeCandidates(const QStringList &photoIds) const
{
    QStringList ids;
    for (const QString &id : photoIds) {
        const QString n = normalizeId(id);
        if (!ids.contains(n))
            ids << n;
    }
    ids = ids.mid(0, imaging::kMergeMaxSources + 1);
    std::vector<Photo> ordered;
    for (const QString &id : ids) {
        if (auto p = photo(id); p && p->isActive())
            ordered.push_back(*p);
    }
    if (ordered.size() < 2)
        throw ActionError(QStringLiteral("merge.tooFew"));
    // A versão Instagram já parte do merge da origem: não entra em outro.
    for (const auto &p : ordered) {
        if (p.isInstagram())
            throw ActionError(QStringLiteral("merge.instagramVersion"));
    }
    QSet<QString> taken = mergeHiddenIds();
    for (const auto &m : liveMerges())
        taken.insert(m.base);
    QStringList busy;
    for (const auto &p : ordered) {
        if (taken.contains(p.id))
            busy << p.fileName;
    }
    if (!busy.isEmpty())
        throw ActionError(QStringLiteral("merge.alreadyMerged"), {{QStringLiteral("name"), busy.join(QStringLiteral(", "))}});
    return ordered;
}

MergeRecord Project::createMerge(const std::vector<Photo> &ordered, const std::vector<QJsonObject> &aligns)
{
    Q_ASSERT(ordered.size() >= 2 && aligns.size() == ordered.size() - 1);
    // A base entra na rampa: a opacidade dela vale quando houver área nela.
    const std::vector<double> opacities = imaging::defaultOpacities(static_cast<int>(ordered.size()));
    MergeRecord m;
    m.id = Database::newId();
    m.base = ordered.front().id;
    m.baseOpacity = opacities.front();
    for (size_t i = 1; i < ordered.size(); ++i)
        m.layers.push_back({ordered[i].id, QString(), opacities[i], true, aligns[i - 1]});
    m_db.exec(QStringLiteral("INSERT INTO photoeditor_merge (id, created, updated, enabled, layers, base_id, base_mask, "
                             "base_opacity) VALUES (?, ?, ?, 1, ?, ?, '', ?)"),
              {m.id, Database::timestamp(), Database::timestamp(),
               QString::fromUtf8(QJsonDocument(layersToJson(m.layers)).toJson(QJsonDocument::Compact)), m.base,
               m.baseOpacity});
    qCInfo(lcMerge) << "Merge created on" << ordered.front().fileName << "with" << m.layers.size() << "layers";
    return m;
}

void Project::saveMerge(const MergeRecord &m)
{
    m_db.exec(QStringLiteral("UPDATE photoeditor_merge SET layers = ?, base_mask = ?, base_opacity = ?, updated = ? "
                             "WHERE id = ?"),
              {QString::fromUtf8(QJsonDocument(layersToJson(m.layers)).toJson(QJsonDocument::Compact)), m.baseMask,
               m.baseOpacity, Database::timestamp(), m.id});
}

std::vector<std::pair<QString, QString>> Project::mergePendingAnalyses(const MergeRecord &merge,
                                                                       const QJsonArray &items) const
{
    std::vector<std::pair<QString, QString>> out;
    for (const auto &v : items) {
        const QJsonObject it = v.toObject();
        const QString id = normalizeId(it.value(QStringLiteral("photo")).toString());
        if (!it.contains(QStringLiteral("mask")) || id == merge.base)
            continue;
        const QString key = it.value(QStringLiteral("mask")).toString();
        for (const auto &e : merge.layers) {
            if (e.photo == id && !key.isEmpty() && e.align.value(QStringLiteral("mask")).toString() != key)
                out.emplace_back(id, key);
        }
    }
    return out;
}

MergeRecord Project::updateMerge(const MergeRecord &merge, const QJsonArray &items,
                                 const QHash<QString, QJsonObject> &aligns)
{
    MergeRecord m = merge;
    QHash<QString, QJsonObject> byPhoto;
    for (const auto &v : items) {
        if (v.isObject())
            byPhoto.insert(normalizeId(v.toObject().value(QStringLiteral("photo")).toString()), v.toObject());
    }
    auto checkMask = [&](const QString &key) {
        if (!key.isEmpty() && imaging::loadMask(m_paths.masksDir(), key).empty())
            throw ActionError(QStringLiteral("layers.maskNotFound"));
    };
    if (byPhoto.contains(m.base)) {
        const QJsonObject it = byPhoto.value(m.base);
        if (it.contains(QStringLiteral("mask"))) {
            const QString key = it.value(QStringLiteral("mask")).toString();
            checkMask(key);
            m.baseMask = key;
        }
        if (it.contains(QStringLiteral("opacity")))
            m.baseOpacity = round3(clamp01(it.value(QStringLiteral("opacity")), m.baseOpacity));
    }
    for (auto &e : m.layers) {
        if (!byPhoto.contains(e.photo))
            continue;
        const QJsonObject it = byPhoto.value(e.photo);
        if (it.contains(QStringLiteral("mask"))) {
            const QString key = it.value(QStringLiteral("mask")).toString();
            checkMask(key);
            e.mask = key;
            if (aligns.contains(e.photo))
                e.align = aligns.value(e.photo);
        }
        if (it.contains(QStringLiteral("opacity")))
            e.opacity = round3(clamp01(it.value(QStringLiteral("opacity")), e.opacity));
        if (it.contains(QStringLiteral("visible")))
            e.visible = it.value(QStringLiteral("visible")).toBool();
    }
    saveMerge(m);
    return m;
}

bool Project::removeMergeLayer(const MergeRecord &merge, const QString &photoId)
{
    const QString id = normalizeId(photoId);
    MergeRecord m = merge;
    const auto before = m.layers.size();
    std::erase_if(m.layers, [&](const MergeLayer &e) { return e.photo == id; });
    if (m.layers.size() == before)
        throw ActionError(QStringLiteral("merge.notALayer"));
    if (m.layers.empty()) {
        dissolveMerge(m);
        return true;
    }
    saveMerge(m);
    qCInfo(lcMerge) << "Merge" << m.id << ": layer" << id << "removed";
    return false;
}

void Project::dissolveMerge(const MergeRecord &merge)
{
    // A base volta a ser só a foto dela; as máscaras ficam em project_data/masks.
    m_db.exec(QStringLiteral("DELETE FROM photoeditor_merge WHERE id = ?"), {merge.id});
    qCInfo(lcMerge) << "Merge" << merge.id << "dissolved";
}

} // namespace pe

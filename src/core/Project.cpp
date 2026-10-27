// SPDX-License-Identifier: BSD-2-Clause
#include "core/Project.h"

#include "core/FileTime.h"
#include "imaging/ImageIO.h"
#include "imaging/SettingsHash.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSqlRecord>

#include <algorithm>
#include <map>

Q_LOGGING_CATEGORY(lcProject, "photoeditor.project")

namespace pe {

namespace {

const QString kPhotoColumns = QStringLiteral(
    "p.id, p.created, p.file_name, p.deleted_file_name, p.status, p.size_bytes, p.mtime_ns, p.width, p.height, "
    "p.orientation, p.captured_at, p.exported_hash, p.is_cover, p.copy_of_id, p.instagram_of_id, "
    "COALESCE(o.file_name, p.file_name), p.archived_file_name, "
    "COALESCE(o.status, p.status), COALESCE(o.deleted_file_name, p.deleted_file_name), "
    "COALESCE(o.archived_file_name, p.archived_file_name)");

const QString kPhotoFrom = QStringLiteral(
    " FROM photoeditor_photo p LEFT JOIN photoeditor_photo o ON o.id = p.copy_of_id ");

// Ordem da filmstrip (Photo.Meta.ordering): data de captura (sem data por
// último) e nome.
const QString kPhotoOrder = QStringLiteral(" ORDER BY p.captured_at IS NULL, p.captured_at, p.file_name");

// O Photo.objects.filter(...) do catalog.visible(): ativas e, se cópia ou
// versão Instagram, com a original/origem ativa.
const QString kVisibleWhere = QStringLiteral(
    "p.status = 'active'"
    " AND (p.copy_of_id IS NULL OR (SELECT status FROM photoeditor_photo c WHERE c.id = p.copy_of_id) = 'active')"
    " AND (p.instagram_of_id IS NULL OR (SELECT status FROM photoeditor_photo i WHERE i.id = p.instagram_of_id) = 'active')");

QString nowTs()
{
    return Database::timestamp();
}

QJsonObject parseObject(const QVariant &v)
{
    return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}

QJsonArray parseArray(const QVariant &v)
{
    return QJsonDocument::fromJson(v.toString().toUtf8()).array();
}

QString toJsonText(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString toJsonText(const QJsonArray &a)
{
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

/// O ``_free_name`` do trash: ``nome.jpg`` ou ``nome-1.jpg``, ``nome-2.jpg``...
QString freeName(const QString &dir, const QString &fileName)
{
    const int dot = fileName.lastIndexOf(u'.');
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    const QString ext = dot > 0 ? fileName.mid(dot + 1) : QString();
    QString candidate = fileName;
    for (int n = 1; QFileInfo::exists(QDir(dir).filePath(candidate)); ++n)
        candidate = ext.isEmpty() ? QStringLiteral("%1-%2").arg(stem).arg(n) : QStringLiteral("%1-%2.%3").arg(stem).arg(n).arg(ext);
    return candidate;
}

/// Move sem copiar (mesmo volume): o mtime passa intacto e os caches valem.
bool moveFile(const QString &from, const QString &to)
{
    return QFile::rename(from, to);
}

} // namespace

// --------------------------------------------------------------------------- //
// EditState
// --------------------------------------------------------------------------- //

QString EditState::settingsHash() const
{
    // Nos estados reais o Python sempre renormaliza um dict com todas as chaves:
    // exposure é float mesmo sem edição (o "pristine" do hash nunca se aplica aqui).
    return imaging::settingsHash(values, preset, crop, layers, false, look);
}

bool EditState::isNeutral() const
{
    return imaging::isNeutral(values, preset, layers) && look.isIdentity();
}

QJsonObject EditState::toJson() const
{
    QJsonObject obj{{QStringLiteral("values"), imaging::valuesToJson(values)},
                    {QStringLiteral("preset"), preset},
                    {QStringLiteral("crop"), imaging::cropToJson(crop)},
                    {QStringLiteral("layers"), imaging::layersToJson(layers)}};
    imaging::lookToJson(look, obj);
    return obj;
}

// --------------------------------------------------------------------------- //
// Abrir
// --------------------------------------------------------------------------- //

Project::Project(const QString &root)
    : m_paths(root)
{
    if (!QFileInfo(m_paths.root()).isDir())
        throw ActionError(QStringLiteral("project.notFound"), {{QStringLiteral("path"), root}});
    QDir().mkpath(m_paths.projectDataDir());
    m_lock = std::make_unique<QLockFile>(m_paths.lockFile());
    // Sem tempo de "trava velha": a do processo que morreu o Qt detecta pelo PID.
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0))
        throw ActionError(QStringLiteral("project.locked"), {{QStringLiteral("name"), m_paths.name()}});
    QDir().mkpath(m_paths.deletedDir());
    QDir().mkpath(m_paths.publishDir());
    m_db.open(m_paths.database());
    qCInfo(lcProject) << "Project opened:" << m_paths.root();
}

Project::~Project()
{
    m_db.close();
    if (m_lock)
        m_lock->unlock();
}

QString Project::normalizeId(const QString &id)
{
    QString out = id.trimmed().toLower();
    out.remove(u'-');
    return out;
}

QString Project::dashedId(const QString &id)
{
    const QString h = normalizeId(id);
    if (h.size() != 32)
        return h;
    return h.left(8) + u'-' + h.mid(8, 4) + u'-' + h.mid(12, 4) + u'-' + h.mid(16, 4) + u'-' + h.mid(20);
}

// --------------------------------------------------------------------------- //
// Fotos
// --------------------------------------------------------------------------- //

Photo Project::photoFromQuery(const QSqlQuery &q) const
{
    Photo p;
    p.id = q.value(0).toString();
    p.created = Database::parseTimestamp(q.value(1));
    p.fileName = q.value(2).toString();
    p.deletedFileName = q.value(3).toString();
    p.status = q.value(4).toString();
    p.sizeBytes = q.value(5).toLongLong();
    p.mtimeNs = q.value(6).toLongLong();
    p.width = q.value(7).toInt();
    p.height = q.value(8).toInt();
    p.orientation = q.value(9).toInt();
    p.capturedAt = Database::parseTimestamp(q.value(10));
    p.exportedHash = q.value(11).toString();
    p.isCover = q.value(12).toBool();
    p.copyOf = q.value(13).toString();
    p.instagramOf = q.value(14).toString();
    p.sourceFileName = q.value(15).toString();
    p.archivedFileName = q.value(16).toString();
    // Onde o JPEG está agora: depende do status de quem é dono dele.
    const QString ownerStatus = q.value(17).toString();
    const QString ownerDeleted = q.value(18).toString();
    const QString ownerArchived = q.value(19).toString();
    if (ownerStatus == PhotoStatus::Deleted)
        p.sourcePath = m_paths.deletedPath(ownerDeleted.isEmpty() ? p.sourceFileName : ownerDeleted);
    else if (ownerStatus == PhotoStatus::Archived)
        p.sourcePath = m_paths.archivedPath(ownerArchived.isEmpty() ? p.sourceFileName : ownerArchived);
    else
        p.sourcePath = m_paths.rawPath(p.sourceFileName);
    return p;
}

std::vector<Photo> Project::queryPhotos(const QString &where, std::initializer_list<QVariant> params) const
{
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT ") + kPhotoColumns + kPhotoFrom + QStringLiteral(" WHERE ") + where
                                + kPhotoOrder,
                            params);
    std::vector<Photo> out;
    while (q.next())
        out.push_back(photoFromQuery(q));
    return out;
}

std::optional<Photo> Project::photo(const QString &id) const
{
    auto rows = queryPhotos(QStringLiteral("p.id = ?"), {normalizeId(id)});
    if (rows.empty())
        return std::nullopt;
    return rows.front();
}

Photo Project::requirePhoto(const QString &id) const
{
    auto p = photo(id);
    if (!p)
        throw ActionError(QStringLiteral("error.photoNotFound"));
    return *p;
}

std::vector<Photo> Project::catalogVisible() const
{
    return queryPhotos(kVisibleWhere);
}

std::vector<Photo> Project::visiblePhotos() const
{
    const QSet<QString> hidden = mergeHiddenIds();
    std::vector<Photo> out;
    for (auto &p : catalogVisible()) {
        if (!hidden.contains(p.id))
            out.push_back(std::move(p));
    }
    return out;
}

QString Project::rawPathOf(const Photo &p) const
{
    return p.sourcePath.isEmpty() ? m_paths.rawPath(p.fileName) : p.sourcePath;
}

std::vector<Photo> Project::archivedPhotos() const
{
    return queryPhotos(QStringLiteral("p.status = 'archived'"));
}

std::vector<Photo> Project::trashedPhotos() const
{
    return queryPhotos(QStringLiteral("p.status = 'deleted'"));
}

std::vector<Photo> Project::instagramPhotos() const
{
    return queryPhotos(QStringLiteral("p.status = 'active' AND p.instagram_of_id IS NOT NULL"));
}

int Project::countWithStatus(const QString &status) const
{
    return m_db.scalar(QStringLiteral("SELECT count(*) FROM photoeditor_photo WHERE status = ?"), {status}).toInt();
}

int Project::countInstagram() const
{
    return m_db
        .scalar(QStringLiteral(
            "SELECT count(*) FROM photoeditor_photo WHERE status = 'active' AND instagram_of_id IS NOT NULL"))
        .toInt();
}

EventMeta Project::eventMeta() const
{
    EventMeta meta;
    QSqlQuery q = m_db.exec(
        QStringLiteral("SELECT name, championship, category FROM photoeditor_eventinfo ORDER BY created LIMIT 1"));
    if (q.next()) {
        meta.name = q.value(0).toString();
        meta.championship = q.value(1).toString();
        meta.category = q.value(2).toString();
    }
    return meta;
}

void Project::setEventMeta(const EventMeta &meta)
{
    const QString now = Database::timestamp();
    const QVariant existing =
        m_db.scalar(QStringLiteral("SELECT id FROM photoeditor_eventinfo ORDER BY created LIMIT 1"));
    if (existing.isValid()) {
        m_db.exec(QStringLiteral("UPDATE photoeditor_eventinfo SET name = ?, championship = ?, category = ?, "
                                 "updated = ? WHERE id = ?"),
                  {meta.name, meta.championship, meta.category, now, existing.toString()});
    } else {
        m_db.exec(QStringLiteral("INSERT INTO photoeditor_eventinfo (id, created, updated, enabled, name, "
                                 "championship, category) VALUES (?, ?, ?, 1, ?, ?, ?)"),
                  {Database::newId(), now, now, meta.name, meta.championship, meta.category});
    }
}

// --------------------------------------------------------------------------- //
// Catálogo (services/catalog.py)
// --------------------------------------------------------------------------- //

ScanSummary Project::scan()
{
    ScanSummary summary;
    const QString rawDir = m_paths.rawDir();
    if (!QFileInfo(rawDir).isDir()) {
        qCWarning(lcProject) << "Scan skipped:" << rawDir << "does not exist.";
        return summary;
    }
    std::map<QString, QFileInfo> onDisk;
    for (const QFileInfo &fi : QDir(rawDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        if (imaging::isJpeg(fi.absoluteFilePath()))
            onDisk.emplace(fi.fileName(), fi);
    }
    QHash<QString, Photo> known;
    for (auto &p : queryPhotos(QStringLiteral("p.copy_of_id IS NULL")))
        known.insert(p.fileName, p);
    QSet<QString> copyNames;
    {
        QSqlQuery q = m_db.exec(QStringLiteral("SELECT file_name FROM photoeditor_photo WHERE copy_of_id IS NOT NULL"));
        while (q.next())
            copyNames.insert(q.value(0).toString());
    }

    Transaction tx(m_db);
    for (const auto &[name, fi] : onDisk) {
        if (copyNames.contains(name)) {
            qCWarning(lcProject) << "Scan: raw/" << name
                                 << "has the name of a duplicate in the catalog; skipped. Rename the file to add it.";
            ++summary.errors;
            continue;
        }
        const qint64 mtimeNs = fileMtimeNs(fi.absoluteFilePath());
        const qint64 size = fi.size();
        const auto it = known.find(name);
        if (it != known.end() && it->mtimeNs == mtimeNs && it->sizeBytes == size) {
            if (it->status != PhotoStatus::Active) {
                // Voltou para raw/ por fora do editor (movida à mão).
                m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET status = 'active', deleted_file_name = '', "
                                         "archived_file_name = '', updated = ? WHERE id = ?"),
                          {nowTs(), it->id});
                ++summary.restored;
            }
            continue;
        }
        const auto read = imaging::readInfo(fi.absoluteFilePath());
        if (!read) {
            qCWarning(lcProject) << "Could not read" << fi.absoluteFilePath() << "; skipping.";
            ++summary.errors;
            continue;
        }
        const imaging::JpegInfo &info = *read;
        const QVariant captured = info.capturedAt.isValid() ? QVariant(Database::timestamp(info.capturedAt)) : QVariant();
        if (it == known.end()) {
            m_db.exec(QStringLiteral(
                          "INSERT INTO photoeditor_photo (id, created, updated, enabled, file_name, deleted_file_name, "
                          "status, size_bytes, mtime_ns, width, height, orientation, captured_at, exported_hash, "
                          "is_cover, copy_of_id, instagram_of_id) VALUES (?, ?, ?, 1, ?, '', 'active', ?, ?, ?, ?, ?, ?, "
                          "'', 0, NULL, NULL)"),
                      {Database::newId(), nowTs(), nowTs(), name, size, mtimeNs, info.width, info.height,
                       info.orientation, captured});
            ++summary.added;
        } else {
            m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET size_bytes = ?, mtime_ns = ?, width = ?, height = ?, "
                                     "orientation = ?, captured_at = ?, status = 'active', deleted_file_name = '', "
                                     "updated = ? WHERE id = ?"),
                      {size, mtimeNs, info.width, info.height, info.orientation, captured, nowTs(), it->id});
            // As cópias leem o mesmo arquivo: o mtime novo invalida os
            // derivados delas também.
            m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET size_bytes = ?, mtime_ns = ?, width = ?, height = ?, "
                                     "orientation = ?, captured_at = ?, updated = ? WHERE copy_of_id = ?"),
                      {size, mtimeNs, info.width, info.height, info.orientation, captured, nowTs(), it->id});
            ++summary.updated;
        }
    }
    // Catalogadas que não estão em raw/: excluídas pelo editor continuam
    // "deleted" enquanto o arquivo estiver em deleted/; o resto sumiu.
    for (const Photo &p : std::as_const(known)) {
        if (onDisk.count(p.fileName))
            continue;
        if (p.status == PhotoStatus::Deleted
            && QFileInfo(m_paths.deletedPath(p.deletedFileName.isEmpty() ? p.fileName : p.deletedFileName)).isFile())
            continue;
        if (p.status == PhotoStatus::Archived
            && QFileInfo(m_paths.archivedPath(p.archivedFileName.isEmpty() ? p.fileName : p.archivedFileName)).isFile())
            continue;
        if (p.status != PhotoStatus::Missing) {
            m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET status = 'missing', updated = ? WHERE id = ?"),
                      {nowTs(), p.id});
            ++summary.missing;
        }
    }
    tx.commit();
    summary.total = static_cast<int>(catalogVisible().size());
    qCInfo(lcProject) << "Catalog scan:" << summary.added << "added," << summary.updated << "updated,"
                      << summary.restored << "restored," << summary.missing << "missing," << summary.errors
                      << "errors," << summary.total << "total";
    return summary;
}

// --------------------------------------------------------------------------- //
// Edição (services/editing.py)
// --------------------------------------------------------------------------- //

std::vector<double> Project::cropRatios(const Photo &p) const
{
    return p.isInstagram() ? imaging::instagramRatioValues() : std::vector<double>{};
}

EditState Project::normalizeState(const Photo &p, const imaging::Values &values, const QString &preset,
                                  const imaging::Crop &crop, const QJsonArray &layers, const imaging::Look &look) const
{
    EditState s;
    s.values = imaging::normalize(values);
    s.preset = imaging::normalizePreset(preset);
    s.crop = imaging::normalizeCrop(crop, p.aspect(), cropRatios(p));
    s.layers = imaging::normalizeLayers(layers);
    s.look = imaging::normalizeLook(look);
    return s;
}

EditState Project::stateFromJson(const Photo &p, const QJsonObject &obj) const
{
    // Entradas de antes do crop/das camadas/do look não têm a chave:
    // normalizar dá o quadro inteiro, nenhuma camada e a identidade.
    return normalizeState(p, imaging::valuesFromJson(obj.value(QStringLiteral("values")).toObject()),
                          obj.value(QStringLiteral("preset")).toString(),
                          obj.value(QStringLiteral("crop")).isObject()
                              ? imaging::cropFromJson(obj.value(QStringLiteral("crop")).toObject())
                              : imaging::kCropIdentity,
                          obj.value(QStringLiteral("layers")).toArray(), imaging::lookFromJson(obj));
}

EditState Project::state(const Photo &p) const
{
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT exposure, contrast, highlights, shadows, whites, blacks, temperature, "
                                           "tint, vibrance, saturation, preset, crop_scale, crop_cx, crop_cy, crop_angle, "
                                           "crop_ratio, layers, levels, curves, effects FROM photoeditor_adjustment "
                                           "WHERE photo_id = ?"),
                            {p.id});
    if (!q.next()) {
        EditState s = normalizeState(p, imaging::kNeutral, {}, imaging::kCropIdentity, {}, {});
        s.pristine = true;
        return s;
    }
    imaging::Values values{};
    for (int i = 0; i < imaging::SliderCount; ++i)
        values[i] = q.value(i).toDouble();
    const imaging::Crop crop{q.value(11).toDouble(), q.value(12).toDouble(), q.value(13).toDouble(),
                             q.value(14).toDouble(), q.value(15).toDouble()};
    imaging::Look look;
    look.levels = imaging::levelsFromJson(parseObject(q.value(17)));
    look.curves = imaging::curvesFromJson(parseObject(q.value(18)));
    look.effects = imaging::effectsFromJson(parseArray(q.value(19)));
    return normalizeState(p, values, q.value(10).toString(), crop, parseArray(q.value(16)), look);
}

void Project::saveState(const Photo &p, const EditState &s)
{
    const auto &v = s.values;
    const QVariant exists = m_db.scalar(QStringLiteral("SELECT id FROM photoeditor_adjustment WHERE photo_id = ?"), {p.id});
    if (exists.isValid()) {
        m_db.exec(QStringLiteral("UPDATE photoeditor_adjustment SET exposure = ?, contrast = ?, highlights = ?, "
                                 "shadows = ?, whites = ?, blacks = ?, temperature = ?, tint = ?, vibrance = ?, "
                                 "saturation = ?, preset = ?, crop_scale = ?, crop_cx = ?, crop_cy = ?, crop_angle = ?, "
                                 "crop_ratio = ?, layers = ?, levels = ?, curves = ?, effects = ?, updated = ? "
                                 "WHERE photo_id = ?"),
                  {v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], s.preset, s.crop.scale, s.crop.cx,
                   s.crop.cy, s.crop.angle, s.crop.ratio, toJsonText(imaging::layersToJson(s.layers)),
                   toJsonText(imaging::levelsToJson(s.look.levels)), toJsonText(imaging::curvesToJson(s.look.curves)),
                   toJsonText(imaging::effectsToJson(s.look.effects)), nowTs(), p.id});
    } else {
        m_db.exec(QStringLiteral("INSERT INTO photoeditor_adjustment (id, created, updated, enabled, exposure, contrast, "
                                 "highlights, shadows, whites, blacks, temperature, tint, vibrance, saturation, preset, "
                                 "photo_id, crop_angle, crop_cx, crop_cy, crop_scale, layers, crop_ratio, levels, "
                                 "curves, effects) VALUES "
                                 "(?, ?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                  {Database::newId(), nowTs(), nowTs(), v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9],
                   s.preset, p.id, s.crop.angle, s.crop.cx, s.crop.cy, s.crop.scale,
                   toJsonText(imaging::layersToJson(s.layers)), s.crop.ratio,
                   toJsonText(imaging::levelsToJson(s.look.levels)), toJsonText(imaging::curvesToJson(s.look.curves)),
                   toJsonText(imaging::effectsToJson(s.look.effects))});
    }
}

EditState Project::applyState(const Photo &p, const EditState &requested, const QString &kind)
{
    const EditState before = state(p);
    const EditState after = normalizeState(p, requested.values, requested.preset, requested.crop,
                                           imaging::layersToJson(requested.layers), requested.look);
    if (after == before)
        return before;
    Transaction tx(m_db);
    saveState(p, after);
    record(p, kind, before.toJson(), after.toJson());
    tx.commit();
    return after;
}

EditState Project::setAdjustments(const Photo &p, const imaging::Values &values, const QString &preset,
                                  const std::optional<imaging::Crop> &crop, const std::optional<QJsonArray> &layers,
                                  const std::optional<imaging::Look> &look)
{
    const EditState before = state(p);
    const EditState after = normalizeState(p, values, preset, crop.value_or(before.crop),
                                           layers.value_or(imaging::layersToJson(before.layers)),
                                           look.value_or(before.look));
    // O tipo da ação é o do que mudou SOZINHO; qualquer outra combinação é ajuste.
    // Níveis, curvas e efeitos contam juntos os do restante e os das camadas;
    // "layer" é o resto da camada (área, valores, preset, criar/apagar).
    auto withoutLook = [](std::vector<imaging::Layer> layers) {
        for (auto &l : layers)
            l.look = {};
        return layers;
    };
    // Pares (antes, depois): o restante e as camadas presentes nos dois lados
    // (camada criada/apagada já é "layer").
    std::vector<std::pair<const imaging::Look *, const imaging::Look *>> looks{{&before.look, &after.look}};
    for (const auto &la : after.layers) {
        for (const auto &lb : before.layers) {
            if (lb.id == la.id)
                looks.emplace_back(&lb.look, &la.look);
        }
    }
    auto lookChanged = [&](auto member) {
        return std::any_of(looks.begin(), looks.end(),
                           [&](const auto &pair) { return !((*pair.first).*member == (*pair.second).*member); });
    };
    const std::vector<std::pair<bool, QString>> changes{
        {after.values != before.values, HistoryKind::Adjust},
        {after.preset != before.preset, HistoryKind::Preset},
        {!(after.crop == before.crop), HistoryKind::Crop},
        {withoutLook(after.layers) != withoutLook(before.layers), HistoryKind::Layer},
        {lookChanged(&imaging::Look::levels), HistoryKind::Levels},
        {lookChanged(&imaging::Look::curves), HistoryKind::Curves},
        {lookChanged(&imaging::Look::effects), HistoryKind::Effects},
    };
    QString kind = HistoryKind::Adjust;
    if (std::count_if(changes.begin(), changes.end(), [](const auto &c) { return c.first; }) == 1)
        kind = std::find_if(changes.begin(), changes.end(), [](const auto &c) { return c.first; })->second;
    return applyState(p, after, kind);
}

EditState Project::reset(const Photo &p)
{
    EditState neutral;
    return applyState(p, neutral, HistoryKind::Reset);
}

// --------------------------------------------------------------------------- //
// Exclusão, cópia, capa, versão Instagram
// --------------------------------------------------------------------------- //

QString Project::moveTo(const Photo &p, const QString &status)
{
    // Cópia virtual não tem arquivo: só o status muda.
    if (p.isCopy()) {
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET status = ?, updated = ? WHERE id = ?"),
                  {status, nowTs(), p.id});
        return {};
    }
    const QString source = p.sourcePath.isEmpty() ? m_paths.rawPath(p.fileName) : p.sourcePath;
    if (!QFileInfo(source).isFile())
        throw ActionError(QStringLiteral("error.photoFileMissing"), {{QStringLiteral("name"), p.fileName}});
    QString dir, name;
    if (status == PhotoStatus::Active) {
        // De volta a raw/ com o nome original; ocupado = conflito (nunca sobrescreve).
        dir = m_paths.rawDir();
        name = p.fileName;
        if (QFileInfo::exists(QDir(dir).filePath(name)))
            throw ActionError(QStringLiteral("error.restoreConflict"), {{QStringLiteral("name"), p.fileName}});
    } else {
        dir = status == PhotoStatus::Archived ? m_paths.archivedDir() : m_paths.deletedDir();
        QDir().mkpath(dir);
        name = freeName(dir, p.fileName);
    }
    const QString target = QDir(dir).filePath(name);
    if (!moveFile(source, target))
        throw ActionError(QStringLiteral("error.photoFileMissing"), {{QStringLiteral("name"), p.fileName}});
    try {
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET status = ?, deleted_file_name = ?, archived_file_name = ?, "
                                 "updated = ? WHERE id = ?"),
                  {status, status == PhotoStatus::Deleted ? name : QString(),
                   status == PhotoStatus::Archived ? name : QString(), nowTs(), p.id});
    } catch (...) {
        moveFile(target, source);
        throw;
    }
    return name;
}

QJsonObject Project::placeJson(const QString &status, const QString &name) const
{
    QJsonObject o{{QStringLiteral("status"), status}};
    if (status == PhotoStatus::Deleted && !name.isEmpty())
        o.insert(QStringLiteral("deleted_file_name"), name);
    if (status == PhotoStatus::Archived && !name.isEmpty())
        o.insert(QStringLiteral("archived_file_name"), name);
    return o;
}

void Project::changeStatus(const Photo &p, const QString &status, const QString &kind)
{
    // Excluir, arquivar e restaurar: o arquivo muda de pasta e o histórico
    // guarda de onde veio (o desfazer leva de volta para lá).
    if (p.status == status)
        return;
    const QString beforeName = p.isDeleted() ? p.deletedFileName : (p.isArchived() ? p.archivedFileName : QString());
    Transaction tx(m_db);
    const QString name = moveTo(p, status);
    QJsonObject after = placeJson(status, name);
    if (p.isCopy())
        after.insert(QStringLiteral("copy"), true);
    record(p, kind, placeJson(p.status, beforeName), after);
    tx.commit();
    qCInfo(lcProject) << kind << p.fileName << p.status << "->" << status;
}

void Project::deletePhoto(const Photo &p)
{
    changeStatus(p, PhotoStatus::Deleted, HistoryKind::Delete);
}

void Project::archivePhoto(const Photo &p)
{
    if (!p.isActive())
        return;
    changeStatus(p, PhotoStatus::Archived, HistoryKind::Archive);
}

void Project::restorePhoto(const Photo &p)
{
    if (p.isInstagram() && instagramVersionOf(requirePhoto(p.instagramOf)))
        throw ActionError(QStringLiteral("instagram.versionExists"));
    changeStatus(p, PhotoStatus::Active, HistoryKind::Restore);
}

int Project::purge(const Photo &photo)
{
    // EXCLUSÃO DEFINITIVA (sem desfazer): o arquivo em deleted/, a linha do
    // catálogo e tudo que depende dela — cópias virtuais e versões Instagram
    // (leem o mesmo arquivo), histórico, ajustes, participação em merges e o
    // que já tinha sido exportado para publicar/.
    if (!photo.isDeleted())
        return 0;
    int removed = 0;
    std::vector<Photo> family{photo};
    for (const Photo &dep : queryPhotos(QStringLiteral("p.copy_of_id = ? OR p.instagram_of_id = ?"), {photo.id, photo.id}))
        family.push_back(dep);
    Transaction tx(m_db);
    for (const Photo &p : family) {
        // Exportações que o editor escreveu para ela.
        if (!p.exportedHash.isEmpty()) {
            if (p.isInstagram()) {
                QFile::remove(QDir(m_paths.instagramDir()).filePath(p.fileName));
            } else {
                QFile::remove(QDir(m_paths.publishDir()).filePath(p.fileName));
                QFile::remove(QDir(m_paths.publishDir()).filePath(mergeExportName(p)));
            }
        }
        // Merges: base some = merge some; camada some = sai da lista.
        for (const auto &m : std::vector<MergeRecord>([&] {
                 std::vector<MergeRecord> all;
                 QSqlQuery q = m_db.exec(QStringLiteral("SELECT id, base_id, layers, base_mask, base_opacity FROM "
                                                        "photoeditor_merge"));
                 while (q.next())
                     all.push_back(mergeFromQuery(q));
                 return all;
             }())) {
            if (m.base == p.id) {
                m_db.exec(QStringLiteral("DELETE FROM photoeditor_merge WHERE id = ?"), {m.id});
                continue;
            }
            MergeRecord changed = m;
            std::erase_if(changed.layers, [&](const MergeLayer &e) { return e.photo == p.id; });
            if (changed.layers.size() == m.layers.size())
                continue;
            if (changed.layers.empty())
                m_db.exec(QStringLiteral("DELETE FROM photoeditor_merge WHERE id = ?"), {m.id});
            else
                saveMerge(changed);
        }
        m_db.exec(QStringLiteral("DELETE FROM photoeditor_historyentry WHERE photo_id = ?"), {p.id});
        m_db.exec(QStringLiteral("DELETE FROM photoeditor_adjustment WHERE photo_id = ?"), {p.id});
    }
    // Dependentes antes da original (as FKs apontam para ela).
    for (auto it = family.rbegin(); it != family.rend(); ++it) {
        m_db.exec(QStringLiteral("DELETE FROM photoeditor_photo WHERE id = ?"), {it->id});
        ++removed;
    }
    tx.commit();
    if (!photo.isCopy()) {
        const QString file = m_paths.deletedPath(photo.deletedFileName.isEmpty() ? photo.fileName : photo.deletedFileName);
        if (QFileInfo(file).isFile() && !QFile::remove(file))
            qCWarning(lcProject) << "Could not remove" << file;
    }
    qCInfo(lcProject) << "Purged" << photo.fileName << "(" << removed << "catalog rows)";
    return removed;
}

int Project::emptyTrash()
{
    int count = 0;
    for (const Photo &p : trashedPhotos()) {
        // Já levada junto com a original (cópia/versão de uma foto da lixeira).
        if (!photo(p.id))
            continue;
        purge(*photo(p.id));
        ++count;
    }
    return count;
}

QString Project::copyName(const Photo &root, const QString &suffix) const
{
    // <nome>_copy.<ext>, depois _copy2, _copy3... — livre no catálogo
    // (inclusive entre excluídas) e em raw/.
    const QFileInfo fi(root.fileName);
    const QString stem = fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : u'.' + fi.suffix();
    for (int n = 1;; ++n) {
        const QString candidate = stem + suffix + (n == 1 ? QString() : QString::number(n)) + ext;
        const bool taken = m_db.scalar(QStringLiteral("SELECT count(*) FROM photoeditor_photo WHERE file_name = ?"),
                                       {candidate})
                               .toInt()
            > 0;
        if (!taken && !QFileInfo::exists(m_paths.rawPath(candidate)))
            return candidate;
    }
}

Photo Project::duplicate(const Photo &p)
{
    if (p.isInstagram())
        throw ActionError(QStringLiteral("instagram.notForVersion"));
    const Photo root = p.isCopy() ? requirePhoto(p.copyOf) : p;
    const QString id = Database::newId();
    Transaction tx(m_db);
    m_db.exec(QStringLiteral("INSERT INTO photoeditor_photo (id, created, updated, enabled, file_name, deleted_file_name, "
                             "status, size_bytes, mtime_ns, width, height, orientation, captured_at, exported_hash, "
                             "is_cover, copy_of_id, instagram_of_id) SELECT ?, ?, ?, 1, ?, '', 'active', size_bytes, "
                             "mtime_ns, width, height, orientation, captured_at, '', 0, ?, NULL FROM photoeditor_photo "
                             "WHERE id = ?"),
              {id, nowTs(), nowTs(), copyName(root, QStringLiteral("_copy")), root.id, root.id});
    const Photo copy = requirePhoto(id);
    const EditState s = state(p);
    saveState(copy, normalizeState(copy, s.values, s.preset, s.crop, imaging::layersToJson(s.layers), s.look));
    record(copy, HistoryKind::Duplicate, {{QStringLiteral("status"), PhotoStatus::Deleted}},
           {{QStringLiteral("status"), PhotoStatus::Active}, {QStringLiteral("copy_of"), dashedId(root.id)}});
    tx.commit();
    qCInfo(lcProject) << "Duplicated" << p.fileName << "->" << copy.fileName;
    return copy;
}

std::optional<Photo> Project::cover() const
{
    auto rows = queryPhotos(QStringLiteral("p.is_cover = 1"));
    if (rows.empty())
        return std::nullopt;
    return rows.front();
}

Photo Project::setCover(const Photo &p, bool on)
{
    if (p.isInstagram())
        throw ActionError(QStringLiteral("instagram.notForVersion"));
    const auto before = cover();
    const QString beforeId = before ? before->id : QString();
    const QString afterId = on ? p.id : (beforeId == p.id ? QString() : beforeId);
    if (afterId == beforeId)
        return p;
    Transaction tx(m_db);
    m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET is_cover = 0 WHERE is_cover = 1"));
    if (!afterId.isEmpty())
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET is_cover = 1 WHERE id = ?"), {afterId});
    record(p, HistoryKind::Cover,
           {{QStringLiteral("cover"), beforeId.isEmpty() ? QJsonValue() : QJsonValue(dashedId(beforeId))}},
           {{QStringLiteral("cover"), afterId.isEmpty() ? QJsonValue() : QJsonValue(dashedId(afterId))}});
    tx.commit();
    qCInfo(lcProject) << "Cover" << (on ? "set:" : "cleared:") << p.fileName;
    return requirePhoto(p.id);
}

std::optional<Photo> Project::instagramVersionOf(const Photo &p) const
{
    auto rows = queryPhotos(QStringLiteral("p.instagram_of_id = ? AND p.status = 'active'"), {p.id});
    if (rows.empty())
        return std::nullopt;
    return rows.front();
}

std::pair<Photo, bool> Project::createInstagramVersion(const Photo &p)
{
    if (p.isInstagram())
        throw ActionError(QStringLiteral("instagram.notForVersion"));
    if (auto existing = instagramVersionOf(p))
        return {*existing, false};
    const Photo root = p.isCopy() ? requirePhoto(p.copyOf) : p;
    const EditState s = state(p);
    // O quadro: a proporção do Instagram mais perto do recorte que a foto já
    // tem, no mesmo centro, com o mesmo endireitamento e do tamanho que couber.
    const double ratio = imaging::closestRatio(imaging::cropOutputAspect(s.crop, p.aspect()),
                                               imaging::instagramRatioValues());
    const imaging::Crop crop{s.crop.scale, s.crop.cx, s.crop.cy, imaging::splitAngle(s.crop.angle).second, ratio};
    const QString id = Database::newId();
    Transaction tx(m_db);
    m_db.exec(QStringLiteral("INSERT INTO photoeditor_photo (id, created, updated, enabled, file_name, deleted_file_name, "
                             "status, size_bytes, mtime_ns, width, height, orientation, captured_at, exported_hash, "
                             "is_cover, copy_of_id, instagram_of_id) SELECT ?, ?, ?, 1, ?, '', 'active', size_bytes, "
                             "mtime_ns, width, height, orientation, captured_at, '', 0, ?, ? FROM photoeditor_photo "
                             "WHERE id = ?"),
              {id, nowTs(), nowTs(), copyName(p, QStringLiteral("_instagram")), root.id, p.id, root.id});
    const Photo version = requirePhoto(id);
    saveState(version, normalizeState(version, s.values, s.preset, crop, imaging::layersToJson(s.layers), s.look));
    record(version, HistoryKind::Instagram, {{QStringLiteral("status"), PhotoStatus::Deleted}},
           {{QStringLiteral("status"), PhotoStatus::Active}, {QStringLiteral("instagram_of"), dashedId(p.id)}});
    tx.commit();
    qCInfo(lcProject) << "Instagram version of" << p.fileName << "->" << version.fileName;
    return {version, true};
}

// --------------------------------------------------------------------------- //
// Histórico (services/history.py)
// --------------------------------------------------------------------------- //

void Project::record(const Photo &p, const QString &kind, const QJsonObject &before, const QJsonObject &after)
{
    m_db.exec(QStringLiteral("INSERT INTO photoeditor_historyentry (id, created, updated, enabled, kind, \"before\", \"after\", "
                             "undone_at, photo_id) VALUES (?, ?, ?, 1, ?, ?, ?, NULL, ?)"),
              {Database::newId(), nowTs(), nowTs(), kind, toJsonText(before), toJsonText(after), p.id});
}

std::vector<HistoryEntry> Project::history(const Photo &p) const
{
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT id, photo_id, kind, \"before\", \"after\", created, undone_at FROM "
                                           "photoeditor_historyentry WHERE photo_id = ? ORDER BY created DESC"),
                            {p.id});
    std::vector<HistoryEntry> out;
    while (q.next()) {
        out.push_back({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), parseObject(q.value(3)),
                       parseObject(q.value(4)), Database::parseTimestamp(q.value(5)),
                       Database::parseTimestamp(q.value(6))});
    }
    return out;
}

std::optional<HistoryEntry> Project::undoLast()
{
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT id, photo_id, kind, \"before\", \"after\", created, undone_at FROM "
                                           "photoeditor_historyentry WHERE undone_at IS NULL ORDER BY created DESC "
                                           "LIMIT 1"));
    if (!q.next())
        return std::nullopt;
    HistoryEntry entry{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), parseObject(q.value(3)),
                       parseObject(q.value(4)), Database::parseTimestamp(q.value(5)), {}};
    Transaction tx(m_db);
    undoEntry(entry);
    entry.undoneAt = QDateTime::currentDateTimeUtc();
    m_db.exec(QStringLiteral("UPDATE photoeditor_historyentry SET undone_at = ?, updated = ? WHERE id = ?"),
              {Database::timestamp(entry.undoneAt), nowTs(), entry.id});
    tx.commit();
    qCInfo(lcProject) << "Undone" << entry.kind << "on" << entry.photoId;
    return entry;
}

std::optional<HistoryEntry> Project::nextUndo() const
{
    QSqlQuery q = m_db.exec(QStringLiteral("SELECT id, photo_id, kind, \"before\", \"after\", created, undone_at FROM "
                                           "photoeditor_historyentry WHERE undone_at IS NULL ORDER BY created DESC "
                                           "LIMIT 1"));
    if (!q.next())
        return std::nullopt;
    return HistoryEntry{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), parseObject(q.value(3)),
                        parseObject(q.value(4)), Database::parseTimestamp(q.value(5)), {}};
}

std::optional<HistoryEntry> Project::nextRedo() const
{
    // A desfeita mais recente — e só se nenhuma ação nova veio depois dela.
    QSqlQuery q = m_db.exec(QStringLiteral(
        "SELECT h.id, h.photo_id, h.kind, h.\"before\", h.\"after\", h.created, h.undone_at FROM "
        "photoeditor_historyentry h WHERE h.undone_at IS NOT NULL AND NOT EXISTS (SELECT 1 FROM "
        "photoeditor_historyentry n WHERE n.created > h.undone_at) ORDER BY h.undone_at DESC LIMIT 1"));
    if (!q.next())
        return std::nullopt;
    return HistoryEntry{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), parseObject(q.value(3)),
                        parseObject(q.value(4)), Database::parseTimestamp(q.value(5)),
                        Database::parseTimestamp(q.value(6))};
}

std::optional<HistoryEntry> Project::redoLast()
{
    auto entry = nextRedo();
    if (!entry)
        return std::nullopt;
    Transaction tx(m_db);
    redoEntry(*entry);
    m_db.exec(QStringLiteral("UPDATE photoeditor_historyentry SET undone_at = NULL, updated = ? WHERE id = ?"),
              {nowTs(), entry->id});
    tx.commit();
    entry->undoneAt = {};
    qCInfo(lcProject) << "Redone" << entry->kind << "on" << entry->photoId;
    return entry;
}

void Project::redoEntry(const HistoryEntry &entry)
{
    // O espelho do undoEntry: volta ao ``after`` da ação.
    const Photo p = requirePhoto(entry.photoId);
    const QString &kind = entry.kind;
    if (kind == HistoryKind::Delete || kind == HistoryKind::Archive || kind == HistoryKind::Restore) {
        const QString target = entry.after.value(QStringLiteral("status")).toString(PhotoStatus::Deleted);
        if (target == PhotoStatus::Active && p.isInstagram() && instagramVersionOf(requirePhoto(p.instagramOf)))
            throw ActionError(QStringLiteral("instagram.versionExists"));
        moveTo(p, target);
        return;
    }
    if (kind == HistoryKind::Duplicate || kind == HistoryKind::Instagram) {
        if (p.isInstagram() && instagramVersionOf(requirePhoto(p.instagramOf)))
            throw ActionError(QStringLiteral("instagram.versionExists"));
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET status = 'active', updated = ? WHERE id = ?"),
                  {nowTs(), p.id});
        return;
    }
    if (kind == HistoryKind::Cover) {
        const QJsonValue after = entry.after.value(QStringLiteral("cover"));
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET is_cover = 0 WHERE is_cover = 1"));
        if (after.isString() && !after.toString().isEmpty())
            m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET is_cover = 1 WHERE id = ?"),
                      {normalizeId(after.toString())});
        return;
    }
    saveState(p, stateFromJson(p, entry.after));
}

void Project::undoEntry(const HistoryEntry &entry)
{
    const Photo p = requirePhoto(entry.photoId);
    const QString &kind = entry.kind;
    if (kind == HistoryKind::Delete || kind == HistoryKind::Archive || kind == HistoryKind::Restore) {
        // Volta para onde estava antes (entradas antigas: "active").
        const QString target = entry.before.value(QStringLiteral("status")).toString(PhotoStatus::Active);
        if (target == PhotoStatus::Active && p.isInstagram() && !p.isActive()) {
            const bool other = m_db.scalar(QStringLiteral("SELECT count(*) FROM photoeditor_photo WHERE "
                                                          "instagram_of_id = ? AND status = 'active'"),
                                           {p.instagramOf})
                                   .toInt()
                > 0;
            // A foto ganhou outra versão Instagram depois: uma por foto.
            if (other)
                throw ActionError(QStringLiteral("instagram.versionExists"));
        }
        moveTo(p, target);
        return;
    }
    if (kind == HistoryKind::Duplicate || kind == HistoryKind::Instagram) {
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET status = 'deleted', updated = ? WHERE id = ?"),
                  {nowTs(), p.id});
        return;
    }
    if (kind == HistoryKind::Cover) {
        const QJsonValue before = entry.before.value(QStringLiteral("cover"));
        m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET is_cover = 0 WHERE is_cover = 1"));
        if (before.isString() && !before.toString().isEmpty())
            m_db.exec(QStringLiteral("UPDATE photoeditor_photo SET is_cover = 1 WHERE id = ?"),
                      {normalizeId(before.toString())});
        return;
    }
    // adjust, auto, preset, reset, crop, layer, levels, curves, effects: volta
    // ao ``before`` (stateFromJson completa as chaves que faltam).
    saveState(p, stateFromJson(p, entry.before));
}

} // namespace pe

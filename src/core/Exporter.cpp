// SPDX-License-Identifier: BSD-2-Clause
#include "core/Exporter.h"

#include "core/Derivatives.h"
#include "core/Project.h"
#include "imaging/ImageIO.h"
#include "imaging/Publish.h"
#include "imaging/SettingsHash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(lcExport, "photoeditor.export")

namespace pe {

namespace {

// Nome da capa em publicar/ (pedido do fluxo de publicação, por isso em PT).
const QString kCoverFileName = QStringLiteral("capa.jpg");

QString outputPath(const ProjectPaths &paths, const Photo &p, const QString &mergeVersion)
{
    if (p.isInstagram())
        return QDir(paths.instagramDir()).filePath(p.fileName);
    const QString name = mergeVersion.isEmpty() ? p.fileName : Project::mergeExportName(p);
    return QDir(paths.publishDir()).filePath(name);
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

Exporter::Exporter(Project *project, QObject *parent)
    : QObject(parent)
    , m_project(project)
{
    // Duas fotos por vez: cada render de 1080p segura ~150 MB — mais workers
    // só disputariam memória.
    m_pool.setMaxThreadCount(kWorkers);
}

Exporter::~Exporter()
{
    wait();
}

void Exporter::wait()
{
    m_pool.waitForDone();
}

QString Exporter::exportHash(const Photo &p, const EditState &state, const QString &mergeVersion)
{
    const QString fmt = p.isInstagram() ? QStringLiteral("ig%1").arg(kInstagramExportVersion)
                                        : QString::number(kExportVersion);
    const QString key = QStringLiteral("%1:%2:%3:%4:%5")
                            .arg(fmt, state.settingsHash(), QString::number(p.mtimeNs), QString::number(p.sizeBytes),
                                 mergeVersion);
    return imaging::sha1Hex(key.toUtf8()).left(16);
}

ExportJob Exporter::makeJob(const Photo &p) const
{
    ExportJob job{m_project->paths(), p, m_project->state(p), std::nullopt, {}, {}, {}};
    if (auto m = m_project->mergeForPhoto(p)) {
        job.merge = m_project->mergeSnapshot(*m);
        job.mergeVersion = merges::version(*job.merge);
    }
    job.digest = exportHash(p, job.state, job.mergeVersion);
    job.output = outputPath(job.paths, p, job.mergeVersion);
    return job;
}

QByteArray Exporter::renderJob(const ExportJob &job)
{
    // Recorta na resolução cheia, depois reduz para a caixa, depois revela (as
    // máscaras das camadas passam pelo mesmo crop e pela mesma redução).
    const imaging::Crop &crop = job.state.crop;
    double frame;
    int longSide;
    int quality;
    std::function<cv::Mat(const cv::Mat &)> fit;
    if (job.photo.isInstagram()) {
        const double aspect = job.photo.aspect();
        const cv::Size size = imaging::instagramSize(imaging::cropOutputAspect(crop, aspect));
        const auto [bw, bh] = imaging::cropFrame(crop.ratio, aspect);
        frame = crop.scale * std::min(bw, bh);
        fit = [size](const cv::Mat &rgb) { return imaging::fitExact(rgb, size); };
        longSide = std::max(size.width, size.height);
        quality = imaging::kInstagramQuality;
    } else {
        frame = crop.scale;
        fit = [](const cv::Mat &rgb) { return imaging::fit(rgb); };
        longSide = imaging::kTargetLongSide;
        quality = imaging::kJpegQuality;
    }
    cv::Mat rgb;
    QByteArray exif;
    if (job.merge && !job.mergeVersion.isEmpty()) {
        std::tie(rgb, exif) = merges::renderFull(*job.merge);
    } else {
        const QString raw = job.photo.sourcePath.isEmpty() ? job.paths.rawPath(job.photo.fileName)
                                                           : job.photo.sourcePath;
        auto loaded = imaging::load(raw, frame, longSide);
        rgb = loaded.rgb;
        exif = loaded.exif;
    }
    if (rgb.empty())
        throw std::runtime_error("could not read the photo");
    const cv::Mat out = developImage(rgb, job.state, job.paths.masksDir(), fit);
    return imaging::encode(out, exif, imaging::softwareTag(), quality);
}

bool Exporter::writePhoto(Project &project, const Photo &p)
{
    Exporter helper(&project);
    const ExportJob job = helper.makeJob(p);
    if (p.exportedHash == job.digest && QFileInfo(job.output).isFile())
        return false;
    QDir().mkpath(QFileInfo(job.output).absolutePath());
    if (!imaging::writeAtomic(job.output, renderJob(job)))
        throw std::runtime_error("could not write the export");
    project.db().exec(QStringLiteral("UPDATE photoeditor_photo SET exported_hash = ? WHERE id = ?"), {job.digest, p.id});
    return true;
}

void Exporter::start()
{
    if (m_status.running)
        return;
    m_status = ExportStatus{};
    m_status.running = true;
    QDir().mkpath(m_project->paths().publishDir());
    m_hidden = m_project->mergeHiddenIds();
    std::vector<ExportJob> jobs;
    m_visibleIds.clear();
    for (const Photo &p : m_project->catalogVisible()) {
        if (m_hidden.contains(p.id))
            continue;
        m_visibleIds.insert(p.id);
        jobs.push_back(makeJob(p));
        if (p.isInstagram())
            ++m_status.instagram;
    }
    m_status.total = static_cast<int>(jobs.size());
    qCInfo(lcExport) << "Export started:" << m_status.total << "photos ->" << m_project->paths().publishDir();
    emit progress();
    if (jobs.empty()) {
        finish();
        return;
    }
    QPointer<Exporter> self(this);
    for (ExportJob &job : jobs) {
        m_pool.start([self, job]() {
            bool written = false;
            QString error;
            try {
                if (!(job.photo.exportedHash == job.digest && QFileInfo(job.output).isFile())) {
                    QDir().mkpath(QFileInfo(job.output).absolutePath());
                    if (!imaging::writeAtomic(job.output, renderJob(job)))
                        throw std::runtime_error("could not write the file");
                    written = true;
                }
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
            QMetaObject::invokeMethod(
                self.data(),
                [self, job, written, error]() {
                    if (self)
                        self->jobDone(job, written, error);
                },
                Qt::QueuedConnection);
        });
    }
}

void Exporter::unlinkOutput(const QString &name) const
{
    // Tira de publicar/ um arquivo que o editor escreveu — a menos que ele seja
    // o nome de uma foto ativa do catálogo (então é a exportação dela).
    const bool active = m_project->db()
                            .scalar(QStringLiteral("SELECT count(*) FROM photoeditor_photo WHERE file_name = ? AND "
                                                   "status = 'active'"),
                                    {name})
                            .toInt()
        > 0;
    if (!active)
        QFile::remove(QDir(m_project->paths().publishDir()).filePath(name));
}

void Exporter::jobDone(const ExportJob &job, bool written, const QString &error)
{
    if (!error.isEmpty()) {
        qCWarning(lcExport) << "Export failed for" << job.photo.fileName << ":" << error;
        m_status.errors << job.photo.fileName;
    } else if (written) {
        if (!job.photo.isInstagram()) {
            // Virou (ou deixou de ser) merge: a saída com o outro nome é velha.
            if (!job.mergeVersion.isEmpty())
                QFile::remove(QDir(job.paths.publishDir()).filePath(job.photo.fileName));
            else
                unlinkOutput(Project::mergeExportName(job.photo));
        }
        m_project->db().exec(QStringLiteral("UPDATE photoeditor_photo SET exported_hash = ? WHERE id = ?"),
                             {job.digest, job.photo.id});
        ++m_status.written;
    } else {
        ++m_status.skipped;
    }
    ++m_status.done;
    emit progress();
    if (m_status.done == m_status.total)
        finish();
}

void Exporter::removeStale(const QSet<QString> &hidden)
{
    // Tira de publicar/ o que o editor exportou de fotos que saíram da
    // filmstrip: excluídas, sumidas, cópias sem a original, camadas de merge e
    // versões Instagram tiradas da seleção.
    QSet<QString> visible;
    for (const Photo &p : m_project->catalogVisible())
        visible.insert(p.id);
    QSqlQuery q = m_project->db().exec(QStringLiteral("SELECT id FROM photoeditor_photo WHERE exported_hash != ''"));
    QStringList ids;
    while (q.next())
        ids << q.value(0).toString();
    for (const QString &id : ids) {
        if (visible.contains(id) && !hidden.contains(id))
            continue;
        const auto p = m_project->photo(id);
        if (!p)
            continue;
        if (p->isInstagram()) {
            QFile::remove(QDir(m_project->paths().instagramDir()).filePath(p->fileName));
        } else {
            QFile::remove(QDir(m_project->paths().publishDir()).filePath(p->fileName));
            unlinkOutput(Project::mergeExportName(*p));
        }
        m_project->db().exec(QStringLiteral("UPDATE photoeditor_photo SET exported_hash = '' WHERE id = ?"), {id});
        ++m_status.removed;
    }
}

void Exporter::exportCover(const QSet<QString> &visibleIds)
{
    // Uma foto do catálogo chamada capa.jpg tem a preferência pelo nome — aí a
    // capa não é gravada (e o log avisa).
    const QString target = QDir(m_project->paths().publishDir()).filePath(kCoverFileName);
    const bool clash = m_project->db()
                           .scalar(QStringLiteral("SELECT count(*) FROM photoeditor_photo WHERE lower(file_name) = ? "
                                                  "AND status = 'active'"),
                                   {kCoverFileName})
                           .toInt()
        > 0;
    if (clash) {
        qCWarning(lcExport) << "A photo is named" << kCoverFileName << ": the cover file is not written.";
        return;
    }
    const auto cover = m_project->cover();
    if (!cover || !visibleIds.contains(cover->id) || cover->isInstagram()) {
        QFile::remove(target);
        return;
    }
    const bool merged = !m_project->mergeVersion(m_project->mergeForPhoto(*cover)).isEmpty();
    const QString name = merged ? Project::mergeExportName(*cover) : cover->fileName;
    const QString source = QDir(m_project->paths().publishDir()).filePath(name);
    if (!QFileInfo(source).isFile())
        return;  // a exportação da própria foto falhou (já está nos erros)
    const QByteArray data = readAll(source);
    if (!(QFileInfo(target).isFile() && readAll(target) == data))
        imaging::writeAtomic(target, data);
    m_status.cover = kCoverFileName;
    qCInfo(lcExport) << "Cover exported:" << name << "->" << kCoverFileName;
}

void Exporter::finish()
{
    try {
        removeStale(m_hidden);
        exportCover(m_visibleIds);
    } catch (const std::exception &e) {
        qCWarning(lcExport) << "Export aborted:" << e.what();
        m_status.errors << QStringLiteral("*");
    }
    m_status.running = false;
    qCInfo(lcExport) << "Export finished:" << m_status.total << "total," << m_status.written << "written,"
                     << m_status.skipped << "skipped," << m_status.removed << "removed";
    emit progress();
    emit finished();
}

} // namespace pe

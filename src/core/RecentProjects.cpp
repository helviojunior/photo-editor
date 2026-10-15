// SPDX-License-Identifier: BSD-2-Clause
#include "core/RecentProjects.h"

#include "core/Database.h"
#include "imaging/ImageIO.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>

#include <atomic>

#include "core/FileTime.h"

namespace pe {

namespace {

// Um passo por versão do esquema; o índice + 1 é o user_version resultante.
// Nunca editar um passo publicado — acrescente o próximo.
const char *const kMigrations[] = {
    // 1: projetos abertos. key = caminho canônico (minúsculo no Windows e no
    // macOS, que ignoram maiúsculas), para a mesma pasta não entrar duas vezes.
    "CREATE TABLE project (key TEXT PRIMARY KEY, path TEXT NOT NULL, created_at TEXT NOT NULL, opened_at TEXT NOT NULL);"
    "CREATE INDEX project_opened_at ON project (opened_at DESC);",
};

QString resolve(const QString &path)
{
    const QFileInfo fi(QDir::cleanPath(QDir(path).absolutePath()));
    const QString canonical = fi.canonicalFilePath();
    return QDir::toNativeSeparators(canonical.isEmpty() ? fi.absoluteFilePath() : canonical);
}

/// os.path.normcase: minúsculas e barras invertidas só no Windows. No macOS o
/// Python não normalizava — mantemos o mesmo para as chaves antigas baterem.
QString key(const QString &path)
{
#ifdef Q_OS_WIN
    return resolve(path).toLower().replace(u'/', u'\\');
#else
    return resolve(path);
#endif
}

QString now()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).replace(QLatin1String("Z"), QLatin1String("+00:00"));
}

QSqlQuery run(const QString &connection, const QString &sql, std::initializer_list<QVariant> params = {})
{
    QSqlQuery q(QSqlDatabase::database(connection));
    q.prepare(sql);
    for (const auto &p : params)
        q.addBindValue(p);
    if (!q.exec())
        throw DatabaseError((q.lastError().text() + QStringLiteral(" | ") + sql).toStdString());
    return q;
}

} // namespace

RecentProjects::RecentProjects(const QString &dbPath)
{
    static std::atomic<int> counter{0};
    m_connection = QStringLiteral("photoeditor-app-%1").arg(counter++);
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    db.setDatabaseName(dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
    if (!db.open())
        throw DatabaseError(db.lastError().text().toStdString());
    run(m_connection, QStringLiteral("PRAGMA journal_mode=WAL"));
    QSqlQuery v = run(m_connection, QStringLiteral("PRAGMA user_version"));
    int version = v.next() ? v.value(0).toInt() : 0;
    const int steps = static_cast<int>(std::size(kMigrations));
    for (int step = version; step < steps; ++step) {
        db.transaction();
        for (const QString &sql : QString::fromLatin1(kMigrations[step]).split(u';', Qt::SkipEmptyParts))
            run(m_connection, sql);
        run(m_connection, QStringLiteral("PRAGMA user_version = %1").arg(step + 1));
        db.commit();
    }
}

RecentProjects::~RecentProjects()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

std::vector<RecentProject> RecentProjects::load() const
{
    QSqlQuery q = run(m_connection, QStringLiteral("SELECT path, opened_at FROM project ORDER BY opened_at DESC LIMIT ?"),
                      {kMaxProjects});
    std::vector<RecentProject> out;
    while (q.next())
        out.push_back({q.value(0).toString(), QDateTime::fromString(q.value(1).toString(), Qt::ISODate)});
    return out;
}

void RecentProjects::touch(const QString &project)
{
    const QString ts = now();
    run(m_connection,
        QStringLiteral("INSERT INTO project (key, path, created_at, opened_at) VALUES (?, ?, ?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET path = excluded.path, opened_at = excluded.opened_at"),
        {key(project), resolve(project), ts, ts});
}

void RecentProjects::remove(const QString &project)
{
    run(m_connection, QStringLiteral("DELETE FROM project WHERE key = ?"), {key(project)});
}

std::optional<RecentProject> RecentProjects::find(const QString &project) const
{
    QSqlQuery q = run(m_connection, QStringLiteral("SELECT path, opened_at FROM project WHERE key = ?"), {key(project)});
    if (!q.next())
        return std::nullopt;
    return RecentProject{q.value(0).toString(), QDateTime::fromString(q.value(1).toString(), Qt::ISODate)};
}

QStringList RecentProjects::rawPhotos(const QString &project)
{
    QDir raw(QDir(project).filePath(QStringLiteral("raw")));
    QStringList out;
    for (const QFileInfo &fi : raw.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!fi.fileName().startsWith(u'.') && imaging::isJpeg(fi.absoluteFilePath()))
            out << fi.absoluteFilePath();
    }
    return out;
}

QString RecentProjects::coverSource(const QString &project)
{
    // Lê o catálogo do evento SOMENTE LEITURA: a Home não abre o projeto e não
    // pode travar nem alterar o banco dele.
    const QString dbPath = QDir(project).filePath(QStringLiteral("project_data/db.sqlite3"));
    if (QFileInfo(dbPath).isFile()) {
        static std::atomic<int> counter{0};
        const QString conn = QStringLiteral("photoeditor-cover-%1").arg(counter++);
        QString chosen;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(dbPath);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
            if (db.open()) {
                QSqlQuery q(db);
                // Capa que é cópia virtual lê o JPEG da original.
                if (q.exec(QStringLiteral(
                        "SELECT COALESCE(o.file_name, p.file_name) FROM photoeditor_photo p "
                        "LEFT JOIN photoeditor_photo o ON o.id = p.copy_of_id "
                        "WHERE p.is_cover = 1 AND p.status = 'active' LIMIT 1"))
                    && q.next())
                    chosen = QDir(project).filePath(QStringLiteral("raw/") + q.value(0).toString());
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(conn);
        if (!chosen.isEmpty() && QFileInfo(chosen).isFile())
            return chosen;
    }
    const QStringList photos = rawPhotos(project);
    return photos.isEmpty() ? QString() : photos.first();
}

ProjectSummary RecentProjects::summary(const RecentProject &item)
{
    ProjectSummary s;
    s.path = item.path;
    const QFileInfo fi(item.path);
    s.name = fi.fileName().isEmpty() ? item.path : fi.fileName();
    s.openedAt = item.openedAt;
    s.exists = fi.isDir();
    if (!s.exists)
        return s;
    s.hasRaw = QFileInfo(QDir(item.path).filePath(QStringLiteral("raw"))).isDir();
    const QStringList photos = rawPhotos(item.path);
    s.photoCount = static_cast<int>(photos.size());
    for (const QString &p : photos)
        s.sizeBytes += QFileInfo(p).size();
    s.coverPath = coverSource(item.path);
    if (!s.coverPath.isEmpty())
        s.coverKey = QFileInfo(s.coverPath).fileName() + u':' + QString::number(fileMtimeNs(s.coverPath));
    return s;
}

} // namespace pe

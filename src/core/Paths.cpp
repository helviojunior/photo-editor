// SPDX-License-Identifier: BSD-2-Clause
#include "core/Paths.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace pe {

namespace {

QString ensureDir(const QString &path)
{
    QDir().mkpath(path);
    return path;
}

} // namespace

QString AppPaths::dataDir()
{
    const QString env = qEnvironmentVariable("PHOTOEDITOR_DATA_DIR");
    if (!env.isEmpty())
        return ensureDir(QDir(env).absolutePath());
    return ensureDir(QDir::home().filePath(QStringLiteral(".photoe")));
}

QString AppPaths::appDb()
{
    return QDir(dataDir()).filePath(QStringLiteral("photoe.db"));
}

QString AppPaths::coversDir()
{
    return ensureDir(QDir(dataDir()).filePath(QStringLiteral("covers")));
}

QString AppPaths::logsDir()
{
    return ensureDir(QDir(dataDir()).filePath(QStringLiteral("logs")));
}

QString AppPaths::instagramFile()
{
    return QDir(dataDir()).filePath(QStringLiteral("instagram.json"));
}

QString AppPaths::segmentModelDir()
{
    const QString env = qEnvironmentVariable("PHOTOEDITOR_MODELS_DIR");
    const QString sub = QStringLiteral("sam2.1-hiera-tiny");
    if (!env.isEmpty())
        return QDir(env).filePath(sub);
    // Instalado: ao lado do executável (Windows/Linux) ou em Resources (macOS).
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {app.filePath(QStringLiteral("models/") + sub),
                                     app.filePath(QStringLiteral("../share/photoeditor/models/") + sub),
                                     app.filePath(QStringLiteral("../Resources/models/") + sub)}) {
        if (QFileInfo(candidate).isDir())
            return QDir::cleanPath(candidate);
    }
    // Desenvolvimento: o .cache/models/ do repositório (make model).
    QDir dir(app);
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo(dir.filePath(QStringLiteral(".cache/models/") + sub)).isDir())
            return dir.filePath(QStringLiteral(".cache/models/") + sub);
        if (!dir.cdUp())
            break;
    }
    return app.filePath(QStringLiteral("models/") + sub);
}

ProjectPaths::ProjectPaths(const QString &root)
    : m_root(QDir::cleanPath(QDir(root).absolutePath()))
{
}

QString ProjectPaths::name() const
{
    const QString n = QFileInfo(m_root).fileName();
    return n.isEmpty() ? m_root : n;
}

QString ProjectPaths::rawDir() const
{
    return QDir(m_root).filePath(QStringLiteral("raw"));
}

QString ProjectPaths::projectDataDir() const
{
    return QDir(m_root).filePath(QStringLiteral("project_data"));
}

QString ProjectPaths::deletedDir() const
{
    return QDir(m_root).filePath(QStringLiteral("deleted"));
}

QString ProjectPaths::archivedDir() const
{
    return QDir(m_root).filePath(QStringLiteral("archived"));
}

QString ProjectPaths::publishDir() const
{
    // Nome em português: é o pedido do fluxo de publicação do evento.
    return QDir(m_root).filePath(QStringLiteral("publicar"));
}

QString ProjectPaths::instagramDir() const
{
    return QDir(publishDir()).filePath(QStringLiteral("instagram"));
}

QString ProjectPaths::database() const
{
    return QDir(projectDataDir()).filePath(QStringLiteral("db.sqlite3"));
}

QString ProjectPaths::lockFile() const
{
    return QDir(projectDataDir()).filePath(QStringLiteral(".photoeditor.lock"));
}

QString ProjectPaths::masksDir() const
{
    return ensureDir(QDir(projectDataDir()).filePath(QStringLiteral("masks")));
}

QString ProjectPaths::cacheDir(const QString &kind) const
{
    return ensureDir(QDir(projectDataDir()).filePath(QStringLiteral("cache/") + kind));
}

QString ProjectPaths::rawPath(const QString &fileName) const
{
    return QDir(rawDir()).filePath(fileName);
}

QString ProjectPaths::deletedPath(const QString &fileName) const
{
    return QDir(deletedDir()).filePath(fileName);
}

QString ProjectPaths::archivedPath(const QString &fileName) const
{
    return QDir(archivedDir()).filePath(fileName);
}

QString ProjectPaths::projectRoot(const QString &chosen)
{
    QString path = QDir::cleanPath(QDir(chosen).absolutePath());
    if (QFileInfo(path).fileName() == QLatin1String("raw"))
        path = QFileInfo(path).absolutePath();
    return path;
}

} // namespace pe

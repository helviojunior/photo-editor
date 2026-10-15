// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Projetos recentes da Home, no banco da MÁQUINA: ~/.photoe/photoe.db —
// separado do catálogo de cada evento. Esquema versionado por
// PRAGMA user_version, migrado de forma incremental (o mesmo da versão em
// Python: project(key, path, created_at, opened_at)).

#include <QDateTime>
#include <QString>

#include <optional>
#include <vector>

namespace pe {

struct RecentProject
{
    QString path;
    QDateTime openedAt;
};

/// O que o card da Home mostra.
struct ProjectSummary
{
    QString path;
    QString name;
    QString coverPath;  ///< foto da capa (raw/...), '' se não há foto
    QString coverKey;   ///< arquivo + mtime: muda quando a capa muda
    QDateTime openedAt;
    bool exists = false;
    bool hasRaw = false;
    int photoCount = 0;
    qint64 sizeBytes = 0;
};

class RecentProjects
{
public:
    static constexpr int kMaxProjects = 60;

    explicit RecentProjects(const QString &dbPath);
    ~RecentProjects();

    std::vector<RecentProject> load() const;
    void touch(const QString &project);
    void remove(const QString &project);
    std::optional<RecentProject> find(const QString &project) const;

    static ProjectSummary summary(const RecentProject &item);
    /// Os JPEGs de <projeto>/raw, ordenados pelo nome (sem ocultos).
    static QStringList rawPhotos(const QString &project);
    /// A foto do card: a capa marcada no editor; sem ela, a primeira de raw/.
    static QString coverSource(const QString &project);

private:
    QString m_connection;
};

} // namespace pe

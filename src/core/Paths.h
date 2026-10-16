// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Onde mora cada coisa.
//
// Da MÁQUINA, em ~/.photoe/ (igual nos três SOs, também no Windows):
//   photoe.db     lista de projetos da Home
//   covers/       capas dos cards da Home
//   logs/         app.log rotativo
//   instagram.json  sessão do Instagram (modo 0600)
//
// Do EVENTO, na pasta do projeto que a pessoa escolhe:
//   raw/            originais (somente JPEG, nunca alterados)
//   project_data/   db.sqlite3 + caches gerados + máscaras + trava
//   deleted/        fotos excluídas (movidas, nunca apagadas)
//   publicar/       saída do Exportar (publicar/instagram/ = versões do feed)

#include <QDir>
#include <QString>

namespace pe {

class AppPaths
{
public:
    /// ~/.photoe, ou PHOTOEDITOR_DATA_DIR (testes, instalação portátil).
    static QString dataDir();
    static QString appDb();
    static QString coversDir();
    static QString logsDir();
    static QString instagramFile();
    /// Modelo SAM: PHOTOEDITOR_MODELS_DIR, <app>/models ou <repo>/models (dev).
    static QString segmentModelDir();
};

class ProjectPaths
{
public:
    ProjectPaths() = default;
    explicit ProjectPaths(const QString &root);

    bool isValid() const { return !m_root.isEmpty(); }
    QString root() const { return m_root; }
    QString name() const;

    QString rawDir() const;
    QString projectDataDir() const;
    QString deletedDir() const;
    /// Fotos arquivadas (Backspace): fora da filmstrip, guardadas.
    QString archivedDir() const;
    QString publishDir() const;
    QString instagramDir() const;
    QString database() const;
    QString lockFile() const;
    QString masksDir() const;
    /// project_data/cache/<kind>, criado na hora.
    QString cacheDir(const QString &kind) const;

    QString rawPath(const QString &fileName) const;
    QString deletedPath(const QString &fileName) const;
    QString archivedPath(const QString &fileName) const;

    /// Pasta escolhida -> raiz do projeto (escolher a própria raw/ vale a de cima).
    static QString projectRoot(const QString &chosen);

private:
    QString m_root;
};

} // namespace pe

// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Exportar: todas as fotos da filmstrip, com os ajustes, em <projeto>/publicar.
//
// Reexportar só regrava o que mudou: cada foto guarda o exported_hash do que
// foi escrito (arquivo original + ajustes + versão do motor e do formato).
// Igual e com o arquivo no lugar = pula. Foto que saiu da filmstrip tem a
// cópia EXPORTADA removida — só arquivos que o próprio editor escreveu.
//
// Base de merge sai como <base>_merge.jpg; camadas de merge não saem. Versões
// Instagram vão para publicar/instagram/ no tamanho do feed. A capa sai também
// como publicar/capa.jpg (cópia exata do que foi exportado para ela).
//
// A thread principal prepara os trabalhos (banco); dois workers renderizam.

#include "core/Merges.h"
#include "core/Models.h"

#include <QObject>
#include <QStringList>
#include <QThreadPool>

#include <optional>

namespace pe {

class Project;

struct ExportStatus
{
    bool running = false;
    int total = 0;
    int done = 0;
    int written = 0;
    int skipped = 0;
    int removed = 0;
    int instagram = 0;
    QStringList errors;
    QString cover;  ///< "capa.jpg" quando a capa foi gravada
};

/// Uma foto a exportar, com tudo pronto para o worker.
struct ExportJob
{
    ProjectPaths paths;
    Photo photo;
    EditState state;
    std::optional<MergeSnapshot> merge;
    QString mergeVersion;
    QString digest;
    QString output;
};

class Exporter : public QObject
{
    Q_OBJECT

public:
    // Sobe quando o formato de saída muda (caixa, qualidade, EXIF...).
    static constexpr int kExportVersion = 2;  ///< 2: EXIF Software com nome, versão e URL
    static constexpr int kInstagramExportVersion = 1;
    static constexpr int kWorkers = 2;

    explicit Exporter(Project *project, QObject *parent = nullptr);
    ~Exporter() override;

    ExportStatus status() const { return m_status; }
    bool isRunning() const { return m_status.running; }
    /// Dispara a exportação; se já houver uma rodando, não faz nada.
    void start();
    /// Espera os workers (fechar o projeto no meio da exportação).
    void wait();

    /// O hash do que foi (ou seria) escrito para a foto.
    static QString exportHash(const Photo &p, const EditState &state, const QString &mergeVersion);
    /// O JPEG exportado da foto (qualquer thread).
    static QByteArray renderJob(const ExportJob &job);
    /// Exporta ``p`` agora, na thread que chamou (o Publicar no Instagram usa).
    static bool writePhoto(Project &project, const Photo &p);

signals:
    void progress();
    void finished();

private:
    ExportJob makeJob(const Photo &p) const;
    void jobDone(const ExportJob &job, bool written, const QString &error);
    void finish();
    void removeStale(const QSet<QString> &hidden);
    void exportCover(const QSet<QString> &visibleIds);
    void unlinkOutput(const QString &name) const;

    Project *m_project;
    ExportStatus m_status;
    QThreadPool m_pool;
    QSet<QString> m_hidden;
    QSet<QString> m_visibleIds;
};

} // namespace pe

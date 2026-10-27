// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Um evento aberto: a pasta do projeto, o catálogo (db.sqlite3) e as ações
// sobre as fotos — o que antes eram os services/ do backend em Python.
//
// Tudo aqui roda na thread principal (o banco é dela). O que é pesado —
// renderizar, exportar, alinhar um merge — recebe os dados prontos de
// snapshot*() e roda em segundo plano (ver Derivatives, Exporter).

#include "core/Database.h"
#include "core/Models.h"
#include "core/Paths.h"

#include <QHash>
#include <QLockFile>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

namespace pe {

struct MergeLayer
{
    QString photo;         ///< id (hex, sem hífens)
    QString mask;          ///< chave da máscara ('' = sem área ainda)
    double opacity = 1.0;
    bool visible = true;
    QJsonObject align;     ///< {matrix 2x3 na resolução cheia, inliers, error, ok, gain, mask}
};

struct MergeRecord
{
    QString id;
    QString base;
    std::vector<MergeLayer> layers;
    QString baseMask;
    double baseOpacity = 1.0;
};

struct MergeSnapshot;

struct ScanSummary
{
    int added = 0;
    int updated = 0;
    int restored = 0;
    int missing = 0;
    int errors = 0;
    int total = 0;
};

class Project
{
public:
    /// Abre a pasta (raw/ tem de existir), trava e migra o banco.
    /// Lança ActionError("project.locked") se outro processo já a abriu.
    explicit Project(const QString &root);
    ~Project();

    const ProjectPaths &paths() const { return m_paths; }
    Database &db() { return m_db; }

    // --- ids --------------------------------------------------------------- //
    /// Id no formato do banco (32 hex) a partir de qualquer forma (com hífens).
    static QString normalizeId(const QString &id);
    /// str(uuid) do Python: com hífens — o formato dentro dos JSON.
    static QString dashedId(const QString &id);

    // --- fotos ------------------------------------------------------------- //
    std::optional<Photo> photo(const QString &id) const;
    Photo requirePhoto(const QString &id) const;
    /// Fotos da filmstrip: ativas, sem as camadas de merge, ordenadas por
    /// data de captura (sem data por último) e nome. Inclui versões Instagram.
    std::vector<Photo> visiblePhotos() const;
    /// Visíveis de acordo com o catálogo (sem tirar as camadas de merge).
    std::vector<Photo> catalogVisible() const;
    ScanSummary scan();
    QString rawPathOf(const Photo &p) const;

    // --- edição (services/editing.py) -------------------------------------- //
    EditState state(const Photo &p) const;
    /// Proporções permitidas: só as do Instagram na versão Instagram.
    std::vector<double> cropRatios(const Photo &p) const;
    EditState normalizeState(const Photo &p, const imaging::Values &values, const QString &preset,
                             const imaging::Crop &crop, const QJsonArray &layers,
                             const imaging::Look &look) const;
    /// Estado a partir do JSON do histórico (chaves ausentes = identidade).
    EditState stateFromJson(const Photo &p, const QJsonObject &obj) const;
    /// Grava o que o painel mandou. crop/layers/look ausentes = mantém o gravado.
    /// O tipo da ação é o do que mudou sozinho (preset, crop, layer, levels,
    /// curves, effects); qualquer outra combinação é "adjust".
    EditState setAdjustments(const Photo &p, const imaging::Values &values, const QString &preset,
                             const std::optional<imaging::Crop> &crop = std::nullopt,
                             const std::optional<QJsonArray> &layers = std::nullopt,
                             const std::optional<imaging::Look> &look = std::nullopt);
    EditState applyState(const Photo &p, const EditState &after, const QString &kind);
    EditState reset(const Photo &p);
    /// Auto sobre o preview já recortado (ou só na área da camada).
    EditState runAuto(const Photo &p, const QString &layerId = {});

    // --- ações ------------------------------------------------------------- //
    /// Lixeira: o JPEG vai para deleted/ (de raw/ ou de archived/). Desfazível.
    void deletePhoto(const Photo &p);
    /// Arquivo: o JPEG vai de raw/ para archived/; sai da filmstrip e do Exportar.
    void archivePhoto(const Photo &p);
    /// Da lixeira ou do arquivo de volta para raw/ (a filmstrip).
    void restorePhoto(const Photo &p);
    /// EXCLUSÃO DEFINITIVA de uma foto da lixeira (arquivo, catálogo, cópias,
    /// histórico, merges, exportações). Sem desfazer. Devolve as linhas removidas.
    int purge(const Photo &p);
    /// purge de toda a lixeira; devolve quantas fotos saíram.
    int emptyTrash();
    std::vector<Photo> archivedPhotos() const;
    std::vector<Photo> trashedPhotos() const;
    /// As versões Instagram ativas (a visualização "Instagram" da filmstrip).
    std::vector<Photo> instagramPhotos() const;
    int countWithStatus(const QString &status) const;
    /// Quantas versões Instagram ativas existem (o selo da visualização).
    int countInstagram() const;
    /// Dados editáveis do evento (nome de exibição, campeonato, categoria). Se a
    /// tabela ainda não tem linha, devolve campos vazios.
    EventMeta eventMeta() const;
    /// Grava os dados do evento (cria a linha singleton na primeira vez).
    void setEventMeta(const EventMeta &meta);
    Photo duplicate(const Photo &p);
    Photo setCover(const Photo &p, bool on);
    std::optional<Photo> cover() const;
    /// (versão, criada agora).
    std::pair<Photo, bool> createInstagramVersion(const Photo &p);
    std::optional<Photo> instagramVersionOf(const Photo &p) const;

    // --- histórico --------------------------------------------------------- //
    void record(const Photo &p, const QString &kind, const QJsonObject &before, const QJsonObject &after);
    /// Desfaz a última ação ainda não desfeita (de qualquer foto).
    std::optional<HistoryEntry> undoLast();
    /// Refaz a última ação desfeita — só enquanto nenhuma ação nova aconteceu
    /// depois do desfazer (uma ação nova descarta o "refazer", como em todo editor).
    std::optional<HistoryEntry> redoLast();
    /// O que o próximo desfazer/refazer faria (sem fazer).
    std::optional<HistoryEntry> nextUndo() const;
    std::optional<HistoryEntry> nextRedo() const;
    std::vector<HistoryEntry> history(const Photo &p) const;

    // --- merges (services/merges.py) --------------------------------------- //
    std::vector<MergeRecord> liveMerges() const;
    std::optional<MergeRecord> merge(const QString &id) const;
    std::optional<MergeRecord> mergeForBase(const QString &photoId) const;
    /// O merge de onde a edição de ``p`` parte (na versão Instagram, o da origem).
    std::optional<MergeRecord> mergeForPhoto(const Photo &p) const;
    /// Camadas de algum merge (e as versões Instagram delas): fora da filmstrip.
    QSet<QString> mergeHiddenIds() const;
    /// Valida e ordena as fotos de um merge novo (a primeira é a base).
    std::vector<Photo> mergeCandidates(const QStringList &photoIds) const;
    /// Grava o merge; ``aligns[i]`` é o merges::analyze da foto ``ordered[i + 1]``.
    MergeRecord createMerge(const std::vector<Photo> &ordered, const std::vector<QJsonObject> &aligns);
    /// Camadas de ``items`` cuja área nova exige refazer o alinhamento: (foto, máscara).
    std::vector<std::pair<QString, QString>> mergePendingAnalyses(const MergeRecord &merge,
                                                                  const QJsonArray &items) const;
    /// Área, transparência e visibilidade (por foto); ``aligns`` = análises
    /// refeitas (por id da foto) para as de mergePendingAnalyses.
    MergeRecord updateMerge(const MergeRecord &merge, const QJsonArray &items,
                            const QHash<QString, QJsonObject> &aligns = {});
    MergeSnapshot mergeSnapshot(const MergeRecord &merge) const;
    /// true se o merge foi desfeito (era a última camada).
    bool removeMergeLayer(const MergeRecord &merge, const QString &photoId);
    void dissolveMerge(const MergeRecord &merge);
    void saveMerge(const MergeRecord &merge);
    /// Hash de tudo de que a composição depende; '' quando o merge não muda nada.
    QString mergeVersion(const std::optional<MergeRecord> &merge) const;
    static QString mergeExportName(const Photo &p);

    // --- máscaras das camadas ---------------------------------------------- //
    QString masksDir() const { return m_paths.masksDir(); }

private:
    Photo photoFromQuery(const QSqlQuery &q) const;
    std::vector<Photo> queryPhotos(const QString &where, std::initializer_list<QVariant> params = {}) const;
    void saveState(const Photo &p, const EditState &state);
    /// Move o JPEG para a pasta do ``status`` (raw/, archived/, deleted/) e
    /// grava status e nome no catálogo; devolve o nome no destino.
    QString moveTo(const Photo &p, const QString &status);
    QJsonObject placeJson(const QString &status, const QString &name) const;
    void changeStatus(const Photo &p, const QString &status, const QString &kind);
    void undoEntry(const HistoryEntry &entry);
    void redoEntry(const HistoryEntry &entry);
    QString copyName(const Photo &root, const QString &suffix) const;
    MergeRecord mergeFromQuery(const QSqlQuery &q) const;

    ProjectPaths m_paths;
    Database m_db;
    std::unique_ptr<QLockFile> m_lock;
};

} // namespace pe

// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Linhas do catálogo do evento (tabelas photoeditor_* do db.sqlite3).

#include "imaging/Crop.h"
#include "imaging/Develop.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <optional>
#include <stdexcept>
#include <vector>

namespace pe {

namespace PhotoStatus {
inline const QString Active = QStringLiteral("active");
inline const QString Deleted = QStringLiteral("deleted");
inline const QString Missing = QStringLiteral("missing");
inline const QString Archived = QStringLiteral("archived");
} // namespace PhotoStatus

/// Tipos de ação do histórico (HistoryEntry.kind).
namespace HistoryKind {
inline const QString Delete = QStringLiteral("delete");
inline const QString Adjust = QStringLiteral("adjust");
inline const QString Auto = QStringLiteral("auto");
inline const QString Preset = QStringLiteral("preset");
inline const QString Reset = QStringLiteral("reset");
inline const QString Crop = QStringLiteral("crop");
inline const QString Layer = QStringLiteral("layer");
inline const QString Duplicate = QStringLiteral("duplicate");
inline const QString Cover = QStringLiteral("cover");
inline const QString Instagram = QStringLiteral("instagram");
inline const QString Archive = QStringLiteral("archive");
inline const QString Restore = QStringLiteral("restore");
inline const QString Levels = QStringLiteral("levels");
inline const QString Curves = QStringLiteral("curves");
inline const QString Effects = QStringLiteral("effects");
} // namespace HistoryKind

struct Photo
{
    QString id;
    QDateTime created;
    QString fileName;
    QString deletedFileName;
    QString archivedFileName;
    QString status = PhotoStatus::Active;
    qint64 sizeBytes = 0;
    qint64 mtimeNs = 0;
    int width = 0;   ///< já na orientação de exibição
    int height = 0;
    int orientation = 1;
    QDateTime capturedAt;  ///< UTC; inválido = sem data
    QString exportedHash;
    bool isCover = false;
    QString copyOf;       ///< cópia virtual: id da original (dona do arquivo)
    QString instagramOf;  ///< versão Instagram: id da foto de origem

    bool isActive() const { return status == PhotoStatus::Active; }
    bool isArchived() const { return status == PhotoStatus::Archived; }
    bool isDeleted() const { return status == PhotoStatus::Deleted; }
    bool isCopy() const { return !copyOf.isEmpty(); }
    bool isInstagram() const { return !instagramOf.isEmpty(); }
    /// Altura/largura na orientação de exibição — o que o crop preserva.
    double aspect() const { return width && height ? static_cast<double>(height) / width : 1.0; }
    /// Nome do arquivo em raw/ (a cópia lê o da original).
    QString sourceFileName;
    /// Caminho ATUAL do JPEG: raw/, archived/ ou deleted/, conforme o status de
    /// quem é dono do arquivo (a original, numa cópia). Montado na leitura do catálogo.
    QString sourcePath;
};

/// Estado de edição de uma foto: ajustes, preset, crop, camadas e o acabamento
/// (níveis, curvas e efeitos) do restante da foto.
struct EditState
{
    imaging::Values values = imaging::kNeutral;
    QString preset;
    imaging::Crop crop = imaging::kCropIdentity;
    std::vector<imaging::Layer> layers;
    imaging::Look look;
    /// Foto sem linha de ajustes (nunca editada).
    bool pristine = false;

    bool operator==(const EditState &o) const
    {
        return values == o.values && preset == o.preset && crop == o.crop && layers == o.layers && look == o.look;
    }
    QString settingsHash() const;
    bool isNeutral() const;
    /// {"values", "preset", "crop", "layers", "levels", "curves", "effects"} —
    /// o formato do histórico (entradas antigas sem as três últimas = identidade).
    QJsonObject toJson() const;
};

struct HistoryEntry
{
    QString id;
    QString photoId;
    QString kind;
    QJsonObject before;
    QJsonObject after;
    QDateTime created;
    QDateTime undoneAt;
};

/// A ação não pode ser feita ou desfeita agora; ``key`` é a chave de i18n.
class ActionError : public std::runtime_error
{
public:
    explicit ActionError(const QString &key, const QVariantMap &params = {})
        : std::runtime_error(key.toStdString())
        , m_key(key)
        , m_params(params)
    {
    }
    QString key() const { return m_key; }
    QVariantMap params() const { return m_params; }

private:
    QString m_key;
    QVariantMap m_params;
};

} // namespace pe

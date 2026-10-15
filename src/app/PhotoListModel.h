// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Fotos da filmstrip (as visíveis, já na ordem escolhida). As versões
// Instagram ficam no modelo (o editor navega até elas pelo I), mas a
// filmstrip as esconde (role isInstagram).

#include "core/Models.h"

#include <QAbstractListModel>
#include <QSet>
#include <QtQmlIntegration>

#include <vector>

class PhotoListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        FileNameRole,
        WidthRole,
        HeightRole,
        AspectRole,
        ThumbUrlRole,
        CapturedAtRole,
        IsCopyRole,
        IsInstagramRole,
        HasInstagramRole,
        IsCoverRole,
        IsMergeBaseRole,
        PickedRole,
        PickOrderRole,
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_photos.size()); }
    const std::vector<pe::Photo> &photos() const { return m_photos; }
    const pe::Photo &at(int row) const { return m_photos.at(static_cast<size_t>(row)); }
    /// Linha da foto ``id`` (-1 = não está na lista).
    Q_INVOKABLE int indexOf(const QString &id) const;

    void reset(std::vector<pe::Photo> photos, QSet<QString> mergeBases, QSet<QString> withInstagram);
    void setPicked(const QStringList &ids);

signals:
    void countChanged();

private:
    std::vector<pe::Photo> m_photos;
    QSet<QString> m_mergeBases;
    QSet<QString> m_withInstagram;
    QStringList m_picked;
};

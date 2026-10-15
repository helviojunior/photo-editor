// SPDX-License-Identifier: BSD-2-Clause
#include "PhotoListModel.h"

#include "core/Project.h"

int PhotoListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant PhotoListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= count())
        return {};
    const pe::Photo &p = at(index.row());
    switch (role) {
    case IdRole:
        return p.id;
    case FileNameRole:
        return p.fileName;
    case WidthRole:
        return p.width;
    case HeightRole:
        return p.height;
    case AspectRole:
        return p.aspect();
    case ThumbUrlRole:
        return QStringLiteral("image://photo/thumb/%1-%2").arg(p.id).arg(p.mtimeNs);
    case CapturedAtRole:
        return p.capturedAt.isValid() ? QVariant(p.capturedAt.toLocalTime()) : QVariant();
    case IsCopyRole:
        return p.isCopy() && !p.isInstagram();
    case IsInstagramRole:
        return p.isInstagram();
    case HasInstagramRole:
        return m_withInstagram.contains(p.id);
    case IsCoverRole:
        return p.isCover;
    case IsMergeBaseRole:
        return m_mergeBases.contains(p.id);
    case PickedRole:
        return m_picked.contains(p.id);
    case PickOrderRole:
        return m_picked.indexOf(p.id);
    default:
        return {};
    }
}

QHash<int, QByteArray> PhotoListModel::roleNames() const
{
    return {
        {IdRole, "photoId"},
        {FileNameRole, "fileName"},
        {WidthRole, "photoWidth"},
        {HeightRole, "photoHeight"},
        {AspectRole, "aspect"},
        {ThumbUrlRole, "thumbUrl"},
        {CapturedAtRole, "capturedAt"},
        {IsCopyRole, "isCopy"},
        {IsInstagramRole, "isInstagram"},
        {HasInstagramRole, "hasInstagram"},
        {IsCoverRole, "isCover"},
        {IsMergeBaseRole, "isMergeBase"},
        {PickedRole, "picked"},
        {PickOrderRole, "pickOrder"},
    };
}

int PhotoListModel::indexOf(const QString &id) const
{
    const QString n = pe::Project::normalizeId(id);
    for (int i = 0; i < count(); ++i) {
        if (m_photos[static_cast<size_t>(i)].id == n)
            return i;
    }
    return -1;
}

void PhotoListModel::reset(std::vector<pe::Photo> photos, QSet<QString> mergeBases, QSet<QString> withInstagram)
{
    const int before = count();
    beginResetModel();
    m_photos = std::move(photos);
    m_mergeBases = std::move(mergeBases);
    m_withInstagram = std::move(withInstagram);
    endResetModel();
    if (before != count())
        emit countChanged();
}

void PhotoListModel::setPicked(const QStringList &ids)
{
    m_picked = ids;
    if (count() > 0)
        emit dataChanged(index(0), index(count() - 1), {PickedRole, PickOrderRole});
}

// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// A tela do merge (pages/Merge.jsx da versão anterior): camadas alinhadas à
// base, área de cada uma (o mesmo pincel do editor), transparência e
// visibilidade. O preview do resultado é montado no QML: a base e, por cima,
// um PNG por camada (image://photo/cutout/...) do tamanho do preview da base,
// cada um com a sua opacidade — mudar a transparência não custa nada.
//
// Transparência e visibilidade gravam 350 ms depois da última mudança; área
// e remoção gravam na hora (a área nova refaz o alinhamento, em segundo plano).

#include "core/Merges.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <memory>
#include <optional>

class EditorController;

class MergeController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QString mergeId READ mergeId NOTIFY changed)
    /// {id, fileName, width, height, previewUrl, thumbUrl}
    Q_PROPERTY(QVariantMap base READ base NOTIFY changed)
    /// {mask, maskUrl, opacity, cutoutUrl, hasMask}
    Q_PROPERTY(QVariantMap baseLayer READ baseLayer NOTIFY changed)
    /// [{photoId, fileName, thumbUrl, previewUrl, mask, maskUrl, hasMask, opacity,
    ///   visible, active, align {scale, angle, dx, dy, ok, inliers, error}, cutoutUrl}]
    Q_PROPERTY(QVariantList layers READ layers NOTIFY changed)
    Q_PROPERTY(QString exportName READ exportName NOTIFY changed)
    /// Foto selecionada no painel (a base ou uma camada)
    Q_PROPERTY(QString activePhoto READ activePhoto WRITE setActivePhoto NOTIFY changed)
    Q_PROPERTY(QString activePreviewUrl READ activePreviewUrl NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    // Seleção da área (pincel), na foto ativa
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionMaskUrl READ selectionMaskUrl NOTIFY selectionChanged)
    Q_PROPERTY(double selectionCoverage READ selectionCoverage NOTIFY selectionChanged)
    Q_PROPERTY(bool selectionBusy READ selectionBusy NOTIFY selectionChanged)
    /// Recorte da foto ativa com a seleção em construção (o "Resultado" ao vivo)
    Q_PROPERTY(QString selectionCutoutUrl READ selectionCutoutUrl NOTIFY selectionChanged)

public:
    explicit MergeController(EditorController *editor, QObject *parent = nullptr);
    ~MergeController() override;

    bool isOpen() const { return m_merge.has_value(); }
    QString mergeId() const { return m_merge ? m_merge->merge.id : QString(); }
    QVariantMap base() const;
    QVariantMap baseLayer() const;
    QVariantList layers() const;
    QString exportName() const;
    QString activePhoto() const { return m_active; }
    void setActivePhoto(const QString &id);
    QString activePreviewUrl() const;
    bool busy() const { return m_busy; }

    bool selecting() const { return m_selecting; }
    QString selectionMaskUrl() const;
    double selectionCoverage() const;
    bool selectionBusy() const { return m_strokesPending > 0; }
    QString selectionCutoutUrl() const { return m_selecting ? cutoutUrl(m_active, m_selectionMask) : QString(); }

    Q_INVOKABLE void openMerge(const QString &mergeId);
    /// Grava o que falta e fecha (volta ao editor na base).
    Q_INVOKABLE void close();
    Q_INVOKABLE void setOpacity(const QString &photoId, double opacity);
    Q_INVOKABLE void setVisible(const QString &photoId, bool visible);
    /// "fadeIn" (0,25 -> 1, a base primeiro) | "fadeOut" | "solid"
    Q_INVOKABLE void applyRamp(const QString &mode);
    Q_INVOKABLE void removeLayer(const QString &photoId);
    Q_INVOKABLE void dissolve();

    Q_INVOKABLE void beginSelection();
    Q_INVOKABLE void stroke(const QVariantList &points, double radius, const QString &mode, bool smart);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void applySelection();
    Q_INVOKABLE void cancelSelection();

signals:
    void changed();
    void busyChanged();
    void selectionChanged();
    void closed(const QString &basePhotoId);
    void failed(const QString &key, const QVariantMap &params);

private:
    void reload();
    void flush();
    void saveItems(const QJsonArray &items);
    void setBusy(bool busy);
    void fail(const std::exception &e);
    QString cutoutUrl(const QString &photoId, const QString &mask) const;
    QString previewUrl(const pe::Photo &p) const;
    QString thumbUrl(const pe::Photo &p) const;
    QString maskUrl(const QString &key) const;

    EditorController *m_editor;
    std::optional<pe::MergeSnapshot> m_merge;
    QString m_active;
    QHash<QString, QJsonObject> m_dirty;  ///< opacidade/visibilidade por foto, a gravar
    QTimer m_saveTimer;
    bool m_busy = false;

    bool m_selecting = false;
    QString m_selectionMask;
    int m_strokesPending = 0;
    std::shared_ptr<struct SelectionChain> m_chain;
    int m_generation = 0;
};

// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// O editor (pages/Editor.jsx da versão anterior) visto do QML: filmstrip, foto
// atual, revelação (sliders, presets, Auto, crop), camadas, seleção com o
// pincel (SAM), ações da filmstrip, histórico e exportação.
//
// Edição em dois tempos, como antes: arrastar um slider muda só o RASCUNHO
// (setValue — a "Editada" é rerenderizada 120 ms depois do último movimento);
// soltar grava (commit — com histórico, só se mudou de fato).
//
// Erros chegam ao QML pelo sinal ``failed(key, params)``: ``key`` é a chave de
// i18n do erro (a mesma da versão anterior, ex. "error.photoFileMissing").

#include "PhotoListModel.h"

#include "core/Exporter.h"
#include "core/Merges.h"
#include "core/Project.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <memory>

namespace pe::imaging {
class Segmenter;
}

class EditorController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    // Filmstrip
    Q_PROPERTY(PhotoListModel *photos READ photos CONSTANT)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    /// {id, fileName, width, height, aspect, capturedAt, isCopy, isInstagram,
    ///  instagramOf, hasInstagram, isCover, isMergeBase, mergeId, position, total}
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    /// Mensagem da barra da filmstrip: chave de i18n ("status.deleted"...) + parâmetros
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantMap statusParams READ statusParams NOTIFY statusMessageChanged)

    // Imagens da foto atual
    Q_PROPERTY(QString originalUrl READ originalUrl NOTIFY currentChanged)
    Q_PROPERTY(QString editedUrl READ editedUrl NOTIFY editedUrlChanged)

    // Revelação: config fixa
    /// [{name, min, max, step, group}] na ordem do painel
    Q_PROPERTY(QVariantList sliders READ sliders CONSTANT)
    /// [{id, values}]
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    /// [{id, ratio, maxAngle}] — 4:5, 1:1, 1.91:1
    Q_PROPERTY(QVariantList instagramRatios READ instagramRatios CONSTANT)
    Q_PROPERTY(double cropMinScale READ cropMinScale CONSTANT)
    Q_PROPERTY(int maxLayers READ maxLayers CONSTANT)

    // Revelação: estado (rascunho) da camada ativa
    /// {exposure: 0.35, contrast: 20, ...} da camada ativa ('' = restante)
    Q_PROPERTY(QVariantMap values READ values NOTIFY stateChanged)
    Q_PROPERTY(QString preset READ preset NOTIFY stateChanged)
    /// {scale, cx, cy, angle, ratio}
    Q_PROPERTY(QVariantMap crop READ crop NOTIFY stateChanged)
    Q_PROPERTY(bool cropIsIdentity READ cropIsIdentity NOTIFY stateChanged)
    /// Ângulo máximo do quadro (90 no crop normal; 45 em 4:5 e 1:1)
    Q_PROPERTY(double cropMaxAngle READ cropMaxAngle NOTIFY stateChanged)
    /// O recorte cabe no feed (só faz sentido na versão Instagram)
    Q_PROPERTY(bool instagramFeedOk READ instagramFeedOk NOTIFY stateChanged)
    /// [{id, mask, maskUrl, coverage}] — a de baixo primeiro
    Q_PROPERTY(QVariantList layers READ layers NOTIFY stateChanged)
    Q_PROPERTY(QString activeLayer READ activeLayer WRITE setActiveLayer NOTIFY stateChanged)
    Q_PROPERTY(QString activeLayerMaskUrl READ activeLayerMaskUrl NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    // Seleção (pincel)
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectionChanged)
    /// Camada cuja área está sendo editada ('' = criando uma nova)
    Q_PROPERTY(QString selectionLayer READ selectionLayer NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionMaskUrl READ selectionMaskUrl NOTIFY selectionChanged)
    Q_PROPERTY(double selectionCoverage READ selectionCoverage NOTIFY selectionChanged)
    Q_PROPERTY(bool selectionBusy READ selectionBusy NOTIFY selectionChanged)
    Q_PROPERTY(bool smartAvailable READ smartAvailable CONSTANT)

    // Desfazer/refazer (globais: a última ação de qualquer foto)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    /// Tipo da ação que o desfazer/refazer atingiria ("adjust", "delete"...)
    Q_PROPERTY(QString undoKind READ undoKind NOTIFY historyChanged)
    Q_PROPERTY(QString redoKind READ redoKind NOTIFY historyChanged)

    // Histórico e exportação
    /// [{kind, created, undone}] da foto atual, a mais recente primeiro
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    /// {running, total, done, written, skipped, removed, errors, cover, instagram, outputDir, instagramDir}
    Q_PROPERTY(QVariantMap exportStatus READ exportStatus NOTIFY exportStatusChanged)
    Q_PROPERTY(bool hasInstagramVersions READ hasInstagramVersions NOTIFY currentChanged)

    // Merge: marcar fotos na filmstrip
    Q_PROPERTY(bool picking READ picking WRITE setPicking NOTIFY pickingChanged)
    Q_PROPERTY(QStringList picked READ picked NOTIFY pickingChanged)

public:
    explicit EditorController(QObject *parent = nullptr);
    ~EditorController() override;

    /// Projeto aberto (nullptr = nenhum). Recarrega a filmstrip.
    void setProject(pe::Project *project);
    pe::Project *project() const { return m_project; }
    pe::Exporter *exporter() const { return m_exporter.get(); }

    PhotoListModel *photos() { return &m_model; }
    int currentIndex() const { return m_index; }
    void setCurrentIndex(int index);
    QVariantMap current() const;
    QString sortMode() const { return m_sortMode; }
    void setSortMode(const QString &mode);
    QString statusMessage() const { return m_status; }
    QVariantMap statusParams() const { return m_statusParams; }

    QString originalUrl() const;
    QString editedUrl() const { return m_editedUrl; }

    QVariantList sliders() const;
    QVariantList presets() const;
    QVariantList instagramRatios() const;
    double cropMinScale() const;
    int maxLayers() const;

    QVariantMap values() const;
    QString preset() const;
    QVariantMap crop() const;
    bool cropIsIdentity() const;
    double cropMaxAngle() const;
    bool instagramFeedOk() const;
    QVariantList layers() const;
    QString activeLayer() const { return m_activeLayer; }
    void setActiveLayer(const QString &id);
    QString activeLayerMaskUrl() const;
    bool busy() const { return m_busy; }

    bool selecting() const { return m_selecting; }
    QString selectionLayer() const { return m_selectionLayer; }
    QString selectionMaskUrl() const;
    double selectionCoverage() const;
    bool selectionBusy() const { return m_strokesPending > 0; }
    bool smartAvailable() const;

    QVariantList history() const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoKind() const;
    QString redoKind() const;
    QVariantMap exportStatus() const;
    bool hasInstagramVersions() const;

    bool picking() const { return m_picking; }
    void setPicking(bool on);
    QStringList picked() const { return m_picked; }

    /// Recarrega a filmstrip mantendo a foto atual (ou ``focusId``).
    void reload(const QString &focusId = {});
    /// O segmentador (compartilhado com o merge).
    pe::imaging::Segmenter *segmenter();

    // --- navegação ---------------------------------------------------------- //
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void selectPhoto(const QString &id);

    // --- revelação ---------------------------------------------------------- //
    /// Rascunho de um slider da camada ativa (não grava).
    Q_INVOKABLE void setValue(const QString &name, double value);
    /// Grava o rascunho (soltar o slider). Sem mudança, não grava.
    Q_INVOKABLE void commit();
    /// Liga/desliga um preset na camada ativa e grava.
    Q_INVOKABLE void togglePreset(const QString &id);
    Q_INVOKABLE void autoAdjust();
    Q_INVOKABLE void reset();
    /// Rascunho do quadro do crop ({scale, cx, cy, angle}; normalizado aqui).
    Q_INVOKABLE void setCrop(const QVariantMap &crop);
    Q_INVOKABLE void commitCrop();
    Q_INVOKABLE void resetCrop();
    /// Proporção da versão Instagram ("4:5" | "1:1" | "1.91:1").
    Q_INVOKABLE void setCropRatio(const QString &id);
    /// Quadro normalizado para ``crop`` sem gravar nada (o CropEditor usa ao arrastar).
    Q_INVOKABLE QVariantMap normalizeCrop(const QVariantMap &crop) const;
    Q_INVOKABLE double cropMaxScaleFor(double angle) const;

    // --- camadas ------------------------------------------------------------ //
    Q_INVOKABLE void deleteLayer(const QString &id);
    /// Próxima camada (L): restante -> 1 -> 2 -> ... -> restante.
    Q_INVOKABLE void cycleLayer();

    // --- seleção ------------------------------------------------------------ //
    /// Entra no modo seleção para criar uma camada ('') ou editar a área de uma.
    Q_INVOKABLE void beginSelection(const QString &layerId = {});
    /// Um traço do pincel: pontos [[x, y]] em fração da foto, raio em fração do
    /// lado maior, "add" | "subtract". Assíncrono; os traços entram em fila.
    Q_INVOKABLE void stroke(const QVariantList &points, double radius, const QString &mode, bool smart);
    Q_INVOKABLE void clearSelection();
    /// Cria a camada (cópia dos valores atuais) ou troca a área da camada.
    Q_INVOKABLE void applySelection();
    Q_INVOKABLE void cancelSelection();

    // --- ações -------------------------------------------------------------- //
    Q_INVOKABLE void deleteCurrent();
    Q_INVOKABLE void duplicateCurrent();
    Q_INVOKABLE void toggleCover();
    /// I: cria/abre a versão Instagram; dentro dela, volta à foto de origem.
    Q_INVOKABLE void toggleInstagram();
    Q_INVOKABLE void undo();
    /// Refaz a última ação desfeita (enquanto nenhuma ação nova a descartou).
    Q_INVOKABLE void redo();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void startExport();

    // --- merge -------------------------------------------------------------- //
    Q_INVOKABLE void togglePick(const QString &id);
    /// Cria o merge das marcadas (assíncrono: alinhamento). Emite mergeCreated.
    Q_INVOKABLE void createMerge();

signals:
    void currentChanged();
    void sortModeChanged();
    void statusMessageChanged();
    void editedUrlChanged();
    void stateChanged();
    void busyChanged();
    void selectionChanged();
    void historyChanged();
    void exportStatusChanged();
    void exportFinished();
    void pickingChanged();
    /// Erro de uma ação: chave de i18n + parâmetros.
    void failed(const QString &key, const QVariantMap &params);
    void mergeCreated(const QString &mergeId);

private:
    pe::imaging::Values &draftValues();
    QString &draftPreset();
    const pe::Photo *currentPhoto() const;
    void loadCurrent();
    void scheduleRender();
    void updateEditedUrl();
    void saveDraft(bool withCrop, bool withLayers);
    void setStatus(const QString &key, const QVariantMap &params = {});
    void setBusy(bool busy);
    void fail(const std::exception &e);
    void registerThumbnails();
    QString maskUrl(const QString &key) const;
    template<typename Fn>
    void guarded(Fn &&fn);

    pe::Project *m_project = nullptr;
    std::unique_ptr<pe::Exporter> m_exporter;
    std::unique_ptr<pe::imaging::Segmenter> m_segmenter;
    PhotoListModel m_model;
    int m_index = -1;
    QString m_sortMode;
    QString m_status;
    QVariantMap m_statusParams;

    std::optional<pe::MergeSnapshot> m_merge;  ///< merge de onde a foto atual parte
    QString m_mergeVersion;
    pe::EditState m_saved;
    pe::EditState m_draft;
    QString m_activeLayer;
    QString m_editedUrl;
    QTimer m_renderTimer;
    bool m_busy = false;

    bool m_selecting = false;
    QString m_selectionLayer;
    QString m_selectionMask;
    std::shared_ptr<struct SelectionChain> m_chain;
    int m_strokesPending = 0;
    int m_selectionGeneration = 0;

    bool m_picking = false;
    QStringList m_picked;
};

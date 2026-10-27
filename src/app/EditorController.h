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

#include "core/Derivatives.h"
#include "core/Exporter.h"
#include "core/Merges.h"
#include "core/Project.h"

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <atomic>
#include <memory>

namespace pe::imaging {
class Segmenter;
}

class EditorController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    // Filmstrip
    /// O que a filmstrip mostra: "photos" (as vivas, editáveis), "archived"
    /// (arquivadas) ou "trash" (lixeira). Fora de "photos" nada se edita.
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(int archivedCount READ archivedCount NOTIFY countsChanged)
    Q_PROPERTY(int trashCount READ trashCount NOTIFY countsChanged)
    Q_PROPERTY(int instagramCount READ instagramCount NOTIFY countsChanged)
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

    // Acabamento (rascunho) da camada ativa: níveis, curvas e efeitos. Vêm
    // depois dos sliders no motor (imaging/Look.h). Como os sliders, os
    // set*() só mudam o rascunho (render em 120 ms) e o commit() grava; os
    // reset*/toggle*/auto*/pick*/applyPreset gravam na hora.
    /// Os quatro canais, sempre (identidade inclusive):
    /// {rgb|r|g|b: {inBlack: 0..253, gamma: 0.10..9.99, inWhite: 2..255,
    ///              outBlack: 0..255, outWhite: 0..255}} — escala 0..255
    Q_PROPERTY(QVariantMap levels READ levels NOTIFY stateChanged)
    /// Os quatro canais, sempre: {rgb|r|g|b: [[x, y], ...]} com x e y em
    /// [0, 1], ordenados por x; identidade = [[0, 0], [1, 1]]
    Q_PROPERTY(QVariantMap curves READ curves NOTIFY stateChanged)
    /// [{id, amount: 1..100}] — só os ligados, na ordem do catálogo
    Q_PROPERTY(QVariantList effects READ effects NOTIFY stateChanged)
    /// [{id, defaultAmount}] — a biblioteca, na ordem em que é aplicada
    /// (bw, sepia, vintage, fade, matte, vignette, grain)
    Q_PROPERTY(QVariantList effectCatalog READ effectCatalog CONSTANT)
    /// Histograma da "Editada" (rascunho, ~512 px, calculado fora da thread
    /// principal): {r, g, b, l: [256 números 0..1 (raiz quadrada da contagem,
    /// normalizada pelo maior pico fora dos extremos 0 e 255)],
    /// clipShadows: fração de pixels com algum canal em 0,
    /// clipHighlights: fração com algum canal em 255}. {} sem foto.
    Q_PROPERTY(QVariantMap histogram READ histogram NOTIFY histogramChanged)
    /// Dados de disparo do JPEG original (EXIF). Só as chaves que existem:
    /// {iso: 800, focalLength: 85 (mm), aperture: "f/2.8", shutter: "1/200" | "2s",
    ///  camera: "Canon EOS R5", lens: "RF85mm F1.2 L USM"}
    Q_PROPERTY(QVariantMap shootingInfo READ shootingInfo NOTIFY currentChanged)

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
    QString view() const { return m_view; }
    void setView(const QString &view);
    int archivedCount() const { return m_archivedCount; }
    int trashCount() const { return m_trashCount; }
    int instagramCount() const { return m_instagramCount; }
    /// Ações de gestão (arquivar, duplicar, capa, merge) e o layout da filmstrip
    /// (as versões Instagram escondidas) valem só na visualização "photos".
    bool editable() const { return m_view == QLatin1String("photos"); }
    /// Revelação (ajustes, crop, níveis, curvas, efeitos, seleção) também vale na
    /// visualização "Instagram": a versão é editada no lugar, sem sair do filtro.
    bool editing() const { return m_view == QLatin1String("photos") || m_view == QLatin1String("instagram"); }
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
    QVariantMap levels() const;
    QVariantMap curves() const;
    QVariantList effects() const;
    QVariantList effectCatalog() const;
    QVariantMap histogram() const { return m_histogram; }
    QVariantMap shootingInfo() const { return m_shootingInfo; }

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
    /// Liga/desliga um preset na camada ativa e grava. LEGADO: o painel usa
    /// applyPreset; fica para as fotos que já guardam um preset.
    Q_INVOKABLE void togglePreset(const QString &id);
    /// Soma os deslocamentos do preset aos sliders da camada ativa (limitados)
    /// e grava (histórico "preset"). O campo preset não muda: o preset só
    /// move sliders, que a pessoa continua ajustando.
    Q_INVOKABLE void applyPreset(const QString &id);
    /// Conta-gotas do balanço de branco: (x, y) em fração do preview ORIGINAL
    /// inteiro (o painel "Original", sem crop). Amostra ~1% do lado maior (no
    /// mínimo 5x5 px) do original — que é exatamente a entrada da etapa de
    /// balanço de branco, a primeira do motor — e acha temperature/tint que
    /// deixam a amostra neutra (descontado o preset legado, se houver). Grava
    /// na camada ativa (histórico "adjust"). Amostra estourada ou quase preta:
    /// failed("wb.badSample").
    Q_INVOKABLE void pickWhite(double x, double y);
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

    // --- níveis, curvas e efeitos (camada ativa) ---------------------------- //
    /// Rascunho dos níveis de um canal ("rgb" | "r" | "g" | "b"); ``levels`` =
    /// {inBlack, gamma, inWhite, outBlack, outWhite} (chaves ausentes = as
    /// atuais). Normalizado aqui (inWhite >= inBlack + 2). commit() grava.
    Q_INVOKABLE void setLevels(const QString &channel, const QVariantMap &levels);
    /// Níveis automáticos do canal: corta 0,1% em cada ponta do histograma do
    /// que CHEGA aos níveis desse canal (rgb: na luminância; r/g/b: no canal) e
    /// grava inBlack/inWhite (gamma e saída ficam). Histórico "levels".
    Q_INVOKABLE void autoLevels(const QString &channel);
    /// Canal de volta à identidade; grava.
    Q_INVOKABLE void resetLevels(const QString &channel);
    /// Rascunho da curva de um canal: [[x, y], ...] (ou [{x, y}]) em [0, 1],
    /// 2 a 16 pontos; ordenada e sem x repetido aqui. commit() grava.
    Q_INVOKABLE void setCurve(const QString &channel, const QVariantList &points);
    /// Curva de volta à identidade; grava.
    Q_INVOKABLE void resetCurve(const QString &channel);
    /// Rascunho da intensidade de um efeito (0..100; 0 desliga). commit() grava.
    Q_INVOKABLE void setEffect(const QString &id, int amount);
    /// Liga (com o defaultAmount) ou desliga um efeito; grava.
    Q_INVOKABLE void toggleEffect(const QString &id);

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
    /// Del: para a lixeira (das fotos ou do arquivo).
    Q_INVOKABLE void deleteCurrent();
    /// Backspace: para o arquivo (archived/).
    Q_INVOKABLE void archiveCurrent();
    /// Do arquivo ou da lixeira de volta para a filmstrip.
    Q_INVOKABLE void restoreCurrent();
    /// Exclusão DEFINITIVA da foto atual da lixeira (sem desfazer).
    Q_INVOKABLE void purgeCurrent();
    /// Exclusão DEFINITIVA de toda a lixeira (sem desfazer).
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE void duplicateCurrent();
    Q_INVOKABLE void toggleCover();
    /// I: cria/abre a versão Instagram; dentro dela, volta à foto de origem.
    Q_INVOKABLE void toggleInstagram();
    /// Desfaz a marcação: manda a versão Instagram da foto atual (ou da sua
    /// origem) para a lixeira. Desfazível (restaurável da lixeira).
    Q_INVOKABLE void removeInstagramVersion();
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
    void viewChanged();
    void countsChanged();
    void sortModeChanged();
    void statusMessageChanged();
    void editedUrlChanged();
    void stateChanged();
    void histogramChanged();
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
    pe::imaging::Look &draftLook();
    const pe::imaging::Look &activeLook() const;
    /// Grava o rascunho com um tipo de ação fixo (o crop fica o gravado).
    void saveAs(const QString &kind);
    pe::RenderRequest renderRequest(const pe::EditState &state) const;
    void scheduleHistogram();
    void loadShootingInfo();
    const pe::Photo *currentPhoto() const;
    void loadCurrent();
    void scheduleRender();
    void updateEditedUrl();
    void saveDraft(bool withCrop, bool withLayers);
    void setStatus(const QString &key, const QVariantMap &params = {});
    void setBusy(bool busy);
    void fail(const std::exception &e);
    void registerThumbnails(const std::vector<pe::Photo> &photos);
    QString maskUrl(const QString &key) const;
    template<typename Fn>
    void guarded(Fn &&fn);

    pe::Project *m_project = nullptr;
    std::unique_ptr<pe::Exporter> m_exporter;
    std::unique_ptr<pe::imaging::Segmenter> m_segmenter;
    PhotoListModel m_model;
    int m_index = -1;
    QString m_view = QStringLiteral("photos");
    int m_archivedCount = 0;
    int m_trashCount = 0;
    int m_instagramCount = 0;
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
    QVariantMap m_histogram;
    QVariantMap m_shootingInfo;
    /// Só o pedido mais recente vale: os anteriores nem chegam a rodar.
    std::shared_ptr<std::atomic<int>> m_histogramLatest = std::make_shared<std::atomic<int>>(0);
    QThreadPool m_histogramPool;

    bool m_selecting = false;
    QString m_selectionLayer;
    QString m_selectionMask;
    std::shared_ptr<struct SelectionChain> m_chain;
    int m_strokesPending = 0;
    int m_selectionGeneration = 0;

    bool m_picking = false;
    QStringList m_picked;
};

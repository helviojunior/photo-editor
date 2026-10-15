// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// O app visto do QML (singleton ``App``): projeto aberto, Home (projetos
// recentes), idioma, tema, tela atual e as ações que antes eram do shell
// desktop (abrir/criar projeto, mostrar pasta, abrir logs).
//
// Abrir um projeto é um diálogo em etapas, todo no QML (modais do próprio app,
// regra 1): inspectFolder() diz o que a pasta tem; o QML pergunta o que for
// preciso (criar raw/, mover as fotos soltas) e chama prepareFolder() e
// openProject().

#include "EditorController.h"
#include "InstagramController.h"
#include "MergeController.h"

#include "core/Project.h"
#include "core/RecentProjects.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <memory>

class AppController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString brandName READ brandName CONSTANT)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged)
    /// "home" | "editor" | "merge" | "instagram"
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    /// [{path, name, coverUrl, openedAt, exists, hasRaw, photoCount, sizeBytes, isCurrent}]
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    /// Mensagem do carregamento (abrindo projeto, copiando fotos...); '' = nada
    Q_PROPERTY(QString loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVariantMap loadingParams READ loadingParams NOTIFY loadingChanged)
    Q_PROPERTY(EditorController *editor READ editor CONSTANT)
    Q_PROPERTY(MergeController *merge READ merge CONSTANT)
    Q_PROPERTY(InstagramController *instagram READ instagram CONSTANT)
    Q_PROPERTY(QString logsDir READ logsDir CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)

public:
    // Sem argumento padrão de propósito: com um construtor sem argumentos o QML
    // criaria um segundo App (vazio) em vez de usar create().
    explicit AppController(QObject *parent);
    ~AppController() override;

    static AppController *instance();
    static AppController *create(QQmlEngine *, QJSEngine *);

    QString version() const;
    QString brandName() const;
    QString language() const;
    QVariantList languages() const;
    bool darkTheme() const { return m_dark; }
    void setDarkTheme(bool dark);
    QString page() const { return m_page; }
    void setPage(const QString &page);
    bool hasProject() const { return m_project != nullptr; }
    QString projectName() const;
    QString projectPath() const;
    QVariantList recentProjects() const { return m_recent; }
    QString loading() const { return m_loading; }
    QVariantMap loadingParams() const { return m_loadingParams; }
    EditorController *editor() { return &m_editor; }
    MergeController *merge() { return &m_merge; }
    InstagramController *instagram() { return &m_instagram; }
    QString logsDir() const;
    QString dataDir() const;
    bool isExporting() const;

    Q_INVOKABLE void setLanguage(const QString &code);
    Q_INVOKABLE void reloadRecent();
    Q_INVOKABLE void removeRecent(const QString &path);

    /// O que uma pasta tem: {path (raiz do projeto), exists, hasRaw, looseJpegs, empty, isProject}.
    Q_INVOKABLE QVariantMap inspectFolder(const QUrl &folder) const;
    /// Cria raw/ e, com ``moveLoose``, move os JPEGs soltos para ela (por
    /// rename, sem sobrescrever). Devolve {ok, moved, failed: [nomes]}.
    Q_INVOKABLE QVariantMap prepareFolder(const QString &path, bool moveLoose);
    /// Copia as fotos escolhidas para <projeto>/raw (assíncrono; loading mostra o progresso).
    Q_INVOKABLE void importPhotos(const QString &path, const QList<QUrl> &files);
    /// Abre o projeto (fecha o atual). Erros chegam por ``failed``.
    Q_INVOKABLE bool openProject(const QString &path);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE void revealInFolder(const QString &path);
    Q_INVOKABLE void openLogsFolder();
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void openMerge(const QString &mergeId);
    /// Texto para a área de transferência (legenda do Instagram).
    Q_INVOKABLE void copyText(const QString &text);
    /// Tamanho legível ("1.2 GB"), base 1024, 1 casa abaixo de 100.
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;

signals:
    void languageChanged();
    void darkThemeChanged();
    void pageChanged();
    void projectChanged();
    void recentProjectsChanged();
    void loadingChanged();
    void importFinished(const QString &path, int copied, const QStringList &failed);
    /// Erro de uma ação: chave de i18n + parâmetros (o QML mostra o modal).
    void failed(const QString &key, const QVariantMap &params);

private:
    void setLoading(const QString &key, const QVariantMap &params = {});

    std::unique_ptr<pe::RecentProjects> m_recentDb;
    std::unique_ptr<pe::Project> m_project;
    EditorController m_editor;
    MergeController m_merge;
    InstagramController m_instagram;
    QVariantList m_recent;
    QString m_page = QStringLiteral("home");
    bool m_dark = true;
    QString m_loading;
    QVariantMap m_loadingParams;
};

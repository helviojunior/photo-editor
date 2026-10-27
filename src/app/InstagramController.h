// SPDX-License-Identifier: BSD-2-Clause
#pragma once

// Conta e publicação no Instagram. A conversa com o Instagram (API privada do
// app do celular, via instagrapi) é a ÚNICA parte que continua em Python: um
// processo auxiliar (src/instagram/photoe_instagram.py) que fala JSON por
// linha no stdin/stdout — sem servidor HTTP. O resto (exportar as versões,
// validar o feed, gravar o post no catálogo) é daqui.
//
// O auxiliar sobe na primeira vez que a tela precisa dele e morre com o app
// (stdin fechado). Sem Python/instagrapi empacotados, ``available`` é falso e
// a tela explica.

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <functional>

class EditorController;

class InstagramController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(QString username READ username NOTIFY statusChanged)
    Q_PROPERTY(QString captionTemplate READ captionTemplate NOTIFY statusChanged)
    Q_PROPERTY(QString defaultTemplate READ defaultTemplate NOTIFY statusChanged)
    Q_PROPERTY(int maxPhotos READ maxPhotos CONSTANT)
    /// Quantos posts (carrosséis) um envio pode gerar de uma vez.
    Q_PROPERTY(int maxPosts READ maxPosts CONSTANT)
    Q_PROPERTY(int captionMax READ captionMax CONSTANT)
    /// {state: ''|running|code|done|error|cancelled, kind: two_factor|challenge, error, detail}
    Q_PROPERTY(QVariantMap login READ login NOTIFY loginChanged)
    /// {running, phase: export|upload, done, total, post, posts, error, detail,
    /// params, url, urls, doneCount}
    Q_PROPERTY(QVariantMap publish READ publish NOTIFY publishChanged)
    /// Versões Instagram da filmstrip: [{id, fileName, thumbUrl, feedOk, aspect, cropRatio}]
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
    /// {name, championship, category, first, last, photos} — para preencher a legenda
    Q_PROPERTY(QVariantMap eventInfo READ eventInfo NOTIFY candidatesChanged)

public:
    static constexpr int kMaxPhotos = 10;
    /// Instagram só aceita 10 imagens por post; acima disso, vários posts.
    static constexpr int kMaxPosts = 10;
    static constexpr int kCaptionMax = 2200;

    explicit InstagramController(EditorController *editor, QObject *parent = nullptr);
    ~InstagramController() override;

    bool available() const;
    bool connected() const { return m_status.value(QStringLiteral("connected")).toBool(); }
    QString username() const { return m_status.value(QStringLiteral("username")).toString(); }
    QString captionTemplate() const;
    QString defaultTemplate() const;
    int maxPhotos() const { return kMaxPhotos; }
    int maxPosts() const { return kMaxPosts; }
    int captionMax() const { return kCaptionMax; }
    QVariantMap login() const { return m_login; }
    QVariantMap publish() const { return m_publish; }
    QVariantList candidates() const;
    QVariantMap eventInfo() const;

    /// Pergunta o estado da conta ao auxiliar (sobe o processo se preciso).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startLogin(const QString &username, const QString &password);
    Q_INVOKABLE void submitCode(const QString &code);
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void logout();
    /// Vazio = volta ao modelo padrão.
    Q_INVOKABLE void saveCaptionTemplate(const QString &text);
    /// Exporta as versões (na ordem) e publica: um post (1 foto), um carrossel
    /// (2–10) ou, acima de 10, vários posts seguidos (lotes de 10), cada um com
    /// a legenda numerada ("Parte 1/N").
    Q_INVOKABLE void startPublish(const QStringList &photoIds, const QString &caption);
    Q_INVOKABLE void resetPublish();

signals:
    void statusChanged();
    void loginChanged();
    void publishChanged();
    void candidatesChanged();
    void failed(const QString &key, const QVariantMap &params);

private:
    using Reply = std::function<void(const QJsonObject &reply)>;

    bool ensureProcess();
    void send(const QString &cmd, const QJsonObject &args, Reply onReply = {});
    void readOutput();
    void handle(const QJsonObject &message);
    void applyStatus(const QJsonObject &result);
    void applyLogin(const QJsonObject &login);
    void setPublish(const QVariantMap &state);
    void uploadFiles(const QStringList &files, const QString &caption, const QStringList &ids);
    /// Sobe o lote ``index`` (atualiza o andamento e manda os arquivos dele).
    void publishBatch(int index);
    /// Legenda do lote: a base, ou "Parte i/N" + base quando há vários posts.
    QString captionForBatch(int index) const;
    /// Grava no catálogo o post recém-publicado (lote atual).
    void recordPost(const QJsonObject &message);

    static QString pythonExecutable();
    static QString helperScript();

    EditorController *m_editor;
    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    int m_nextId = 1;
    QHash<int, Reply> m_pending;
    QVariantMap m_status;
    QVariantMap m_login;
    int m_loginId = 0;
    QVariantMap m_publish;
    int m_publishId = 0;
    QList<QStringList> m_publishBatches;  ///< ids (normalizados) por post
    QList<QStringList> m_publishFiles;    ///< arquivos a enviar por post
    QString m_publishCaption;             ///< legenda base (sem o sufixo de parte)
    int m_publishBatch = 0;               ///< lote em envio
    QStringList m_publishUrls;            ///< urls dos posts já publicados
};

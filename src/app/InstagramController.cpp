// SPDX-License-Identifier: BSD-2-Clause
#include "InstagramController.h"

#include "EditorController.h"

#include "core/Database.h"
#include "core/Exporter.h"
#include "core/Paths.h"
#include "core/Project.h"
#include "imaging/Crop.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(lcInstagram, "photoeditor.instagram")

using namespace pe;

namespace {

const QString kDefaultTemplate = QStringLiteral("{event}\n{date}\n\n{hashtag}");

/// Onde o pacote põe o auxiliar: <app>/instagram (Windows/Linux) ou
/// Contents/Resources/instagram (macOS); no desenvolvimento, src/instagram.
QString helperDir()
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &candidate :
         {app.filePath(QStringLiteral("instagram")), app.filePath(QStringLiteral("../share/photoeditor/instagram")),
          app.filePath(QStringLiteral("../Resources/instagram"))}) {
        if (QFileInfo::exists(candidate + QStringLiteral("/photoe_instagram.py")))
            return QDir::cleanPath(candidate);
    }
    QDir dir(app);
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("src/instagram/photoe_instagram.py"))))
            return dir.filePath(QStringLiteral("src/instagram"));
        if (!dir.cdUp())
            break;
    }
    return {};
}

} // namespace

InstagramController::InstagramController(EditorController *editor, QObject *parent)
    : QObject(parent)
    , m_editor(editor)
{
    connect(editor, &EditorController::currentChanged, this, &InstagramController::candidatesChanged);
}

InstagramController::~InstagramController()
{
    if (m_process) {
        // stdin fechado = o auxiliar sai sozinho; um upload no meio é abandonado.
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(2000))
            m_process->kill();
    }
}

QString InstagramController::pythonExecutable()
{
    const QString env = qEnvironmentVariable("PHOTOEDITOR_PYTHON");
    if (!env.isEmpty())
        return env;
    const QString dir = helperDir();
#ifdef Q_OS_WIN
    const QString bundled = dir + QStringLiteral("/python/python.exe");
#else
    const QString bundled = dir + QStringLiteral("/python/bin/python3");
#endif
    if (!dir.isEmpty() && QFileInfo(bundled).isExecutable())
        return bundled;
    // Desenvolvimento: o Python do sistema, com o instagrapi instalado.
    return QStandardPaths::findExecutable(QStringLiteral("python3"));
}

QString InstagramController::helperScript()
{
    const QString dir = helperDir();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/photoe_instagram.py");
}

bool InstagramController::available() const
{
    return !helperScript().isEmpty() && !pythonExecutable().isEmpty() && m_status.value(QStringLiteral("_ok"), true).toBool();
}

QString InstagramController::captionTemplate() const
{
    const QString t = m_status.value(QStringLiteral("caption_template")).toString();
    return t.isEmpty() ? defaultTemplate() : t;
}

QString InstagramController::defaultTemplate() const
{
    const QString t = m_status.value(QStringLiteral("default_template")).toString();
    return t.isEmpty() ? kDefaultTemplate : t;
}

bool InstagramController::ensureProcess()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        return true;
    const QString python = pythonExecutable();
    const QString script = helperScript();
    if (python.isEmpty() || script.isEmpty()) {
        qCWarning(lcInstagram) << "Instagram helper unavailable (python:" << python << "script:" << script << ")";
        return false;
    }
    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &InstagramController::readOutput);
        // O stderr do auxiliar é log: drenado sempre, para o pipe nunca encher.
        connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
            const QByteArray err = m_process->readAllStandardError();
            for (const QByteArray &line : err.split('\n')) {
                if (!line.trimmed().isEmpty())
                    qCDebug(lcInstagram).noquote() << QString::fromUtf8(line);
            }
        });
        connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            qCInfo(lcInstagram) << "Instagram helper exited with" << code;
            const auto pending = std::exchange(m_pending, {});
            for (const Reply &reply : pending) {
                if (reply)
                    reply({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("instagram.failed")}});
            }
        });
    }
    m_buffer.clear();
    m_process->start(python, {QStringLiteral("-I"), QStringLiteral("-E"), QStringLiteral("-s"), script,
                              QStringLiteral("--data-dir"), AppPaths::dataDir()});
    if (!m_process->waitForStarted(5000)) {
        qCWarning(lcInstagram) << "Could not start the Instagram helper:" << m_process->errorString();
        return false;
    }
    return true;
}

void InstagramController::send(const QString &cmd, const QJsonObject &args, Reply onReply)
{
    if (!ensureProcess()) {
        if (onReply)
            onReply({{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("instagram.unavailable")}});
        return;
    }
    const int id = m_nextId++;
    if (onReply)
        m_pending.insert(id, std::move(onReply));
    const QJsonObject request{{QStringLiteral("id"), id}, {QStringLiteral("cmd"), cmd}, {QStringLiteral("args"), args}};
    m_process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
}

void InstagramController::readOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl).trimmed();
        m_buffer.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            handle(doc.object());
    }
}

void InstagramController::handle(const QJsonObject &message)
{
    // Eventos podem chegar antes da resposta do comando: roteia por id/evento.
    const QString event = message.value(QStringLiteral("event")).toString();
    // Id -1 = comando enviado, resposta ainda não chegou: o primeiro evento
    // traz o id novo.
    if (event == QLatin1String("login")) {
        const int id = message.value(QStringLiteral("login_id")).toInt();
        if (m_loginId == -1 && message.value(QStringLiteral("state")).toString() != QLatin1String("cancelled"))
            m_loginId = id;
        if (id == m_loginId)
            applyLogin(message);
        return;
    }
    if (event == QLatin1String("publish")) {
        const int id = message.value(QStringLiteral("publish_id")).toInt();
        if (m_publishId == -1)
            m_publishId = id;
        if (id != m_publishId)
            return;
        const QString state = message.value(QStringLiteral("state")).toString();
        QVariantMap p = m_publish;
        if (state == QLatin1String("running")) {
            p[QStringLiteral("phase")] = QStringLiteral("upload");
        } else if (state == QLatin1String("done")) {
            p[QStringLiteral("running")] = false;
            p[QStringLiteral("url")] = message.value(QStringLiteral("url")).toString();
            // Grava o post no catálogo (photoeditor_instagrampost).
            if (Project *project = m_editor->project()) {
                QJsonArray ids;
                for (const QString &id : m_publishIds)
                    ids.append(Project::dashedId(id));
                try {
                    project->db().exec(
                        QStringLiteral("INSERT INTO photoeditor_instagrampost (id, created, updated, enabled, username, "
                                       "media_id, code, caption, photos) VALUES (?, ?, ?, 1, ?, ?, ?, ?, ?)"),
                        {Database::newId(), Database::timestamp(), Database::timestamp(),
                         message.value(QStringLiteral("username")).toString(),
                         message.value(QStringLiteral("media_id")).toVariant().toString(),
                         message.value(QStringLiteral("code")).toString(), m_publishCaption,
                         QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact))});
                } catch (const std::exception &e) {
                    qCWarning(lcInstagram) << "Could not record the post:" << e.what();
                }
            }
            qCInfo(lcInstagram) << "Published:" << p.value(QStringLiteral("url")).toString();
        } else if (state == QLatin1String("error")) {
            p[QStringLiteral("running")] = false;
            p[QStringLiteral("error")] = message.value(QStringLiteral("error")).toString();
            p[QStringLiteral("detail")] = message.value(QStringLiteral("detail")).toString();
        }
        setPublish(p);
        return;
    }
    const int id = message.value(QStringLiteral("id")).toInt(-1);
    if (const Reply reply = m_pending.take(id))
        reply(message);
}

void InstagramController::applyStatus(const QJsonObject &result)
{
    m_status = result.toVariantMap();
    if (result.value(QStringLiteral("login")).isObject())
        applyLogin(result.value(QStringLiteral("login")).toObject());
    emit statusChanged();
}

void InstagramController::applyLogin(const QJsonObject &login)
{
    m_loginId = login.value(QStringLiteral("login_id")).toInt(m_loginId);
    m_login = login.toVariantMap();
    emit loginChanged();
    if (login.value(QStringLiteral("state")).toString() == QLatin1String("done"))
        refresh();
}

void InstagramController::refresh()
{
    send(QStringLiteral("status"), {}, [this](const QJsonObject &reply) {
        if (reply.value(QStringLiteral("ok")).toBool())
            applyStatus(reply.value(QStringLiteral("result")).toObject());
        else
            emit statusChanged();
    });
}

void InstagramController::startLogin(const QString &username, const QString &password)
{
    m_loginId = -1;
    send(QStringLiteral("login"), {{QStringLiteral("username"), username}, {QStringLiteral("password"), password}},
         [this](const QJsonObject &reply) {
             if (reply.value(QStringLiteral("ok")).toBool())
                 applyLogin(reply.value(QStringLiteral("result")).toObject());
             else
                 emit failed(reply.value(QStringLiteral("error")).toString(), {});
         });
}

void InstagramController::submitCode(const QString &code)
{
    send(QStringLiteral("code"), {{QStringLiteral("code"), code}}, [this](const QJsonObject &reply) {
        if (reply.value(QStringLiteral("ok")).toBool())
            applyLogin(reply.value(QStringLiteral("result")).toObject());
        else
            emit failed(reply.value(QStringLiteral("error")).toString(), {});
    });
}

void InstagramController::cancelLogin()
{
    send(QStringLiteral("cancel"), {});
}

void InstagramController::logout()
{
    send(QStringLiteral("logout"), {}, [this](const QJsonObject &reply) {
        if (reply.value(QStringLiteral("ok")).toBool())
            applyStatus(reply.value(QStringLiteral("result")).toObject());
    });
}

void InstagramController::saveCaptionTemplate(const QString &text)
{
    send(QStringLiteral("set_template"), {{QStringLiteral("text"), text}}, [this](const QJsonObject &reply) {
        if (reply.value(QStringLiteral("ok")).toBool())
            applyStatus(reply.value(QStringLiteral("result")).toObject());
    });
}

QVariantList InstagramController::candidates() const
{
    QVariantList out;
    Project *project = m_editor->project();
    if (!project)
        return out;
    for (const Photo &p : m_editor->photos()->photos()) {
        if (!p.isInstagram())
            continue;
        const EditState s = project->state(p);
        out << QVariantMap{{QStringLiteral("id"), p.id},
                           {QStringLiteral("fileName"), p.fileName},
                           {QStringLiteral("aspect"), imaging::cropOutputAspect(s.crop, p.aspect())},
                           {QStringLiteral("cropRatio"), s.crop.ratio},
                           {QStringLiteral("feedOk"), imaging::instagramFeedOk(s.crop, p.aspect())},
                           {QStringLiteral("thumbUrl"), QStringLiteral("image://photo/thumb/%1-%2").arg(p.id).arg(p.mtimeNs)}};
    }
    return out;
}

QVariantMap InstagramController::eventInfo() const
{
    Project *project = m_editor->project();
    if (!project)
        return {};
    // Nome do evento (a pasta), período das fotos e quantas são.
    QDateTime first, last;
    int count = 0;
    for (const Photo &p : m_editor->photos()->photos()) {
        if (p.isInstagram())
            continue;
        ++count;
        if (!p.capturedAt.isValid())
            continue;
        if (!first.isValid() || p.capturedAt < first)
            first = p.capturedAt;
        if (!last.isValid() || p.capturedAt > last)
            last = p.capturedAt;
    }
    return {{QStringLiteral("name"), project->paths().name()},
            {QStringLiteral("first"), first.isValid() ? QVariant(first.toLocalTime()) : QVariant()},
            {QStringLiteral("last"), last.isValid() ? QVariant(last.toLocalTime()) : QVariant()},
            {QStringLiteral("photos"), count}};
}

void InstagramController::setPublish(const QVariantMap &state)
{
    m_publish = state;
    emit publishChanged();
}

void InstagramController::resetPublish()
{
    if (!m_publish.value(QStringLiteral("running")).toBool())
        setPublish({});
}

void InstagramController::startPublish(const QStringList &photoIds, const QString &caption)
{
    Project *project = m_editor->project();
    if (!project || m_publish.value(QStringLiteral("running")).toBool())
        return;
    QStringList ids;
    for (const QString &id : photoIds) {
        const QString n = Project::normalizeId(id);
        if (!ids.contains(n))
            ids << n;
    }
    // O diálogo de publicação mostra o erro no próprio corpo: sem o alerta
    // global (``failed``), que o duplicaria.
    auto failWith = [this](const QString &key, const QVariantMap &params = {}) {
        setPublish({{QStringLiteral("running"), false}, {QStringLiteral("error"), key}, {QStringLiteral("params"), params}});
    };
    if (ids.size() > kMaxPhotos)
        return failWith(QStringLiteral("instagram.tooMany"), {{QStringLiteral("max"), kMaxPhotos}});
    std::vector<Photo> photos;
    QStringList outside;
    for (const QString &id : ids) {
        auto p = project->photo(id);
        if (!p || !p->isInstagram() || !p->isActive())
            continue;
        if (!imaging::instagramFeedOk(project->state(*p).crop, p->aspect()))
            outside << p->fileName;
        photos.push_back(*p);
    }
    if (photos.empty())
        return failWith(QStringLiteral("instagram.noPhotos"));
    // 1,91:1 girado além de 45° sai vertical (1:1,91): o feed recusa.
    if (!outside.isEmpty())
        return failWith(QStringLiteral("instagram.outOfFeed"), {{QStringLiteral("name"), outside.join(QStringLiteral(", "))}});
    const QString text = caption.trimmed();
    if (text.size() > kCaptionMax)
        return failWith(QStringLiteral("instagram.captionTooLong"), {{QStringLiteral("max"), kCaptionMax}});
    if (!connected())
        return failWith(QStringLiteral("instagram.notConnected"));

    m_publishIds = ids;
    m_publishCaption = text;
    setPublish({{QStringLiteral("running"), true},
                {QStringLiteral("phase"), QStringLiteral("export")},
                {QStringLiteral("done"), 0},
                {QStringLiteral("total"), static_cast<int>(photos.size())}});

    // Exporta as versões (o que já está em dia é pulado). Cada uma leva menos
    // de um segundo; a barra de progresso é atualizada entre elas.
    QStringList files;
    for (const Photo &p : photos)
        files << QDir(project->paths().instagramDir()).filePath(p.fileName);
    for (const Photo &p : photos) {
        try {
            Exporter::writePhoto(*project, p);
        } catch (const std::exception &e) {
            qCWarning(lcInstagram) << "Export before publishing failed:" << e.what();
            return failWith(QStringLiteral("instagram.publishFailed"), {{QStringLiteral("detail"), QString::fromUtf8(e.what())}});
        }
        QVariantMap p2 = m_publish;
        p2[QStringLiteral("done")] = p2.value(QStringLiteral("done")).toInt() + 1;
        setPublish(p2);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    uploadFiles(files, text, ids);
}

void InstagramController::uploadFiles(const QStringList &files, const QString &caption, const QStringList &ids)
{
    Q_UNUSED(ids);
    m_publishId = -1;
    send(QStringLiteral("publish"),
         {{QStringLiteral("files"), QJsonArray::fromStringList(files)}, {QStringLiteral("caption"), caption}},
         [this](const QJsonObject &reply) {
             if (reply.value(QStringLiteral("ok")).toBool()) {
                 m_publishId = reply.value(QStringLiteral("result")).toObject().value(QStringLiteral("publish_id")).toInt();
                 if (m_publish.value(QStringLiteral("running")).toBool()) {
                     QVariantMap p = m_publish;
                     p[QStringLiteral("phase")] = QStringLiteral("upload");
                     setPublish(p);
                 }
             } else {
                 setPublish({{QStringLiteral("running"), false},
                             {QStringLiteral("error"), reply.value(QStringLiteral("error")).toString()},
                             {QStringLiteral("detail"), reply.value(QStringLiteral("detail")).toString()},
                             {QStringLiteral("params"), reply.value(QStringLiteral("params")).toObject().toVariantMap()}});
             }
         });
}

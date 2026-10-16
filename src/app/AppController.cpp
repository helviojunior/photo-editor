// SPDX-License-Identifier: BSD-2-Clause
#include "AppController.h"

#include "ImageProviders.h"
#include "Language.h"

#include "imaging/ImageIO.h"
#include "imaging/SettingsHash.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QProcess>
#include <QSettings>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(lcApp, "photoeditor.app")

using namespace pe;

namespace {

AppController *g_instance = nullptr;

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QStringList looseJpegs(const QString &dir)
{
    QStringList out;
    for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        if (!fi.fileName().startsWith(u'.') && imaging::isJpeg(fi.absoluteFilePath()))
            out << fi.fileName();
    }
    return out;
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_merge(&m_editor)
    , m_instagram(&m_editor)
{
    g_instance = this;
    m_dark = QSettings().value(QStringLiteral("ui/dark"), true).toBool();
    try {
        m_recentDb = std::make_unique<RecentProjects>(AppPaths::appDb());
    } catch (const std::exception &e) {
        qCWarning(lcApp) << "Could not open the projects list:" << e.what();
    }
    connect(&m_editor, &EditorController::failed, this, &AppController::failed);
    connect(&m_merge, &MergeController::failed, this, &AppController::failed);
    connect(&m_instagram, &InstagramController::failed, this, &AppController::failed);
    connect(&m_editor, &EditorController::mergeCreated, this, &AppController::openMerge);
    connect(&m_merge, &MergeController::closed, this, [this](const QString &base) {
        if (!base.isEmpty())
            m_editor.reload(base);
        if (m_page == QLatin1String("merge"))
            setPage(QStringLiteral("editor"));
    });
    reloadRecent();
}

AppController::~AppController()
{
    m_editor.setProject(nullptr);
    m_project.reset();
    g_instance = nullptr;
}

AppController *AppController::instance()
{
    return g_instance;
}

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    // O main cria a instância antes do QML; o motor não é dono dela.
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString AppController::version() const
{
    return QCoreApplication::applicationVersion();
}

QString AppController::brandName() const
{
    return QStringLiteral(PHOTOEDITOR_BRAND_NAME);
}

QString AppController::language() const
{
    return app::activeLanguage();
}

QVariantList AppController::languages() const
{
    return app::availableLanguages();
}

void AppController::setLanguage(const QString &code)
{
    app::saveLanguage(code);
    app::installLanguage(code);
    // O QML retraduz tudo (qsTr) com o retranslate do motor, no main.
    emit languageChanged();
}

void AppController::setDarkTheme(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    QSettings().setValue(QStringLiteral("ui/dark"), dark);
    emit darkThemeChanged();
}

void AppController::setPage(const QString &page)
{
    if (page == m_page)
        return;
    m_page = page;
    emit pageChanged();
}

QString AppController::projectName() const
{
    return m_project ? m_project->paths().name() : QString();
}

QString AppController::projectPath() const
{
    return m_project ? QDir::toNativeSeparators(m_project->paths().root()) : QString();
}

QString AppController::logsDir() const
{
    return QDir::toNativeSeparators(AppPaths::logsDir());
}

QString AppController::dataDir() const
{
    return QDir::toNativeSeparators(AppPaths::dataDir());
}

bool AppController::isExporting() const
{
    return m_editor.exporter() && m_editor.exporter()->isRunning();
}

void AppController::setLoading(const QString &key, const QVariantMap &params)
{
    m_loading = key;
    m_loadingParams = params;
    emit loadingChanged();
}

// --------------------------------------------------------------------------- //
// Home
// --------------------------------------------------------------------------- //

void AppController::reloadRecent()
{
    m_recent.clear();
    if (!m_recentDb) {
        emit recentProjectsChanged();
        return;
    }
    const QString current = m_project ? m_project->paths().root() : QString();
    for (const RecentProject &item : m_recentDb->load()) {
        const ProjectSummary s = RecentProjects::summary(item);
        QString coverUrl;
        if (!s.coverPath.isEmpty()) {
            // Capa do card: 480 px em ~/.photoe/covers (fora do projeto).
            const QString key = imaging::sha1Hex((s.path + u'|' + s.coverKey).toUtf8()).left(20);
            const QString cover = s.coverPath;
            ImageRegistry::instance().put(QStringLiteral("file"), key, [cover, key]() {
                const QString cached = QDir(AppPaths::coversDir()).filePath(key + QStringLiteral(".jpg"));
                if (!QFileInfo(cached).isFile()) {
                    const cv::Mat rgb = imaging::loadRgb(cover, 480);
                    if (rgb.empty())
                        return QImage();
                    imaging::writeAtomic(cached, imaging::encodeJpeg(rgb, 82));
                }
                return imagefiles::load(cached);
            });
            coverUrl = QStringLiteral("image://photo/file/") + key;
        }
        m_recent << QVariantMap{{QStringLiteral("path"), QDir::toNativeSeparators(s.path)},
                                {QStringLiteral("name"), s.name},
                                {QStringLiteral("coverUrl"), coverUrl},
                                {QStringLiteral("openedAt"), s.openedAt.toLocalTime()},
                                {QStringLiteral("exists"), s.exists},
                                {QStringLiteral("hasRaw"), s.hasRaw},
                                {QStringLiteral("photoCount"), s.photoCount},
                                {QStringLiteral("sizeBytes"), s.sizeBytes},
                                {QStringLiteral("isCurrent"), !current.isEmpty()
                                                                  && QDir::cleanPath(s.path) == QDir::cleanPath(current)}};
    }
    emit recentProjectsChanged();
}

void AppController::removeRecent(const QString &path)
{
    // Só tira da lista: a pasta e as fotos não são tocadas.
    if (m_recentDb)
        m_recentDb->remove(QDir::fromNativeSeparators(path));
    reloadRecent();
}

QString AppController::shortcutText(const QString &sequence) const
{
    return QKeySequence(sequence, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

bool AppController::sameShortcut(const QString &a, const QString &b) const
{
    const QKeySequence ka(a, QKeySequence::PortableText);
    return !ka.isEmpty() && ka == QKeySequence(b, QKeySequence::PortableText);
}

QString AppController::formatBytes(qint64 bytes) const
{
    const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    const QLocale locale;
    return (u == 0 || v >= 100 ? locale.toString(v, 'f', 0) : locale.toString(v, 'f', 1)) + u' '
        + QLatin1String(units[u]);
}

// --------------------------------------------------------------------------- //
// Abrir / criar projeto
// --------------------------------------------------------------------------- //

QVariantMap AppController::inspectFolder(const QUrl &folder) const
{
    const QString root = ProjectPaths::projectRoot(localPath(folder));
    const QDir dir(root);
    const bool exists = dir.exists();
    const QStringList loose = exists ? looseJpegs(root) : QStringList();
    const bool hasRaw = QFileInfo(dir.filePath(QStringLiteral("raw"))).isDir();
    return {{QStringLiteral("path"), QDir::toNativeSeparators(root)},
            {QStringLiteral("name"), QFileInfo(root).fileName()},
            {QStringLiteral("exists"), exists},
            {QStringLiteral("hasRaw"), hasRaw},
            {QStringLiteral("isProject"), hasRaw},
            {QStringLiteral("looseJpegs"), static_cast<int>(loose.size())},
            // Ocultos (.DS_Store...) não contam: a pasta "vazia" é a sem nada visível.
            {QStringLiteral("empty"), exists && dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()}};
}

QVariantMap AppController::prepareFolder(const QString &path, bool moveLoose)
{
    const QString root = QDir::fromNativeSeparators(path);
    const QString raw = QDir(root).filePath(QStringLiteral("raw"));
    if (!QDir().mkpath(raw))
        return {{QStringLiteral("ok"), false}};
    int moved = 0;
    QStringList failedNames;
    if (moveLoose) {
        // Por rename (mesmo volume, instantâneo), nunca sobrescrevendo.
        for (const QString &name : looseJpegs(root)) {
            const QString target = QDir(raw).filePath(name);
            if (QFileInfo::exists(target) || !QFile::rename(QDir(root).filePath(name), target))
                failedNames << name;
            else
                ++moved;
        }
    }
    qCInfo(lcApp) << "Prepared" << root << ": moved" << moved << "photos into raw/," << failedNames.size() << "failed";
    return {{QStringLiteral("ok"), true}, {QStringLiteral("moved"), moved}, {QStringLiteral("failed"), failedNames}};
}

void AppController::importPhotos(const QString &path, const QList<QUrl> &files)
{
    const QString raw = QDir(QDir::fromNativeSeparators(path)).filePath(QStringLiteral("raw"));
    QDir().mkpath(raw);
    QStringList sources;
    for (const QUrl &u : files)
        sources << localPath(u);
    setLoading(QStringLiteral("loading.importing"), {{QStringLiteral("done"), 0}, {QStringLiteral("total"), sources.size()}});
    QPointer<AppController> self(this);
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, path, sources]() {
        watcher->deleteLater();
        const QStringList failedNames = watcher->result();
        setLoading({});
        emit importFinished(path, static_cast<int>(sources.size() - failedNames.size()), failedNames);
    });
    watcher->setFuture(QtConcurrent::run([self, raw, sources]() {
        QStringList failedNames;
        int done = 0;
        for (const QString &src : sources) {
            const QString name = QFileInfo(src).fileName();
            const QString target = QDir(raw).filePath(name);
            // Cópia (como o copy2 de antes): o original da pessoa fica onde está.
            if (QFileInfo::exists(target) || !QFile::copy(src, target))
                failedNames << name;
            ++done;
            QMetaObject::invokeMethod(
                self.data(),
                [self, done, total = sources.size()]() {
                    if (self)
                        self->setLoading(QStringLiteral("loading.importing"),
                                         {{QStringLiteral("done"), done}, {QStringLiteral("total"), total}});
                },
                Qt::QueuedConnection);
        }
        return failedNames;
    }));
}

bool AppController::openProject(const QString &path)
{
    const QString root = ProjectPaths::projectRoot(QDir::fromNativeSeparators(path));
    if (!QFileInfo(root).isDir()) {
        emit failed(QStringLiteral("project.notFound"), {{QStringLiteral("path"), path}});
        return false;
    }
    if (!QFileInfo(QDir(root).filePath(QStringLiteral("raw"))).isDir()) {
        emit failed(QStringLiteral("project.noRaw"), {{QStringLiteral("path"), path}});
        return false;
    }
    if (m_project && QDir::cleanPath(m_project->paths().root()) == QDir::cleanPath(root)) {
        setPage(QStringLiteral("editor"));
        return true;
    }
    setLoading(QStringLiteral("loading.opening"), {{QStringLiteral("name"), QFileInfo(root).fileName()}});
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    closeProject();
    try {
        auto project = std::make_unique<Project>(root);
        project->scan();
        m_project = std::move(project);
    } catch (const ActionError &e) {
        setLoading({});
        emit failed(e.key(), e.params());
        return false;
    } catch (const std::exception &e) {
        setLoading({});
        qCWarning(lcApp) << "Could not open" << root << ":" << e.what();
        emit failed(QStringLiteral("project.openFailed"),
                    {{QStringLiteral("name"), QFileInfo(root).fileName()}, {QStringLiteral("detail"), QString::fromUtf8(e.what())}});
        return false;
    }
    if (m_recentDb)
        m_recentDb->touch(root);
    m_editor.setProject(m_project.get());
    setLoading({});
    emit projectChanged();
    reloadRecent();
    setPage(QStringLiteral("editor"));
    return true;
}

void AppController::closeProject()
{
    if (!m_project)
        return;
    m_editor.commit();
    if (m_merge.isOpen())
        m_merge.close();
    m_editor.setProject(nullptr);
    m_project.reset();
    ImageRegistry::instance().clear();
    emit projectChanged();
    reloadRecent();
    setPage(QStringLiteral("home"));
}

void AppController::openMerge(const QString &mergeId)
{
    m_merge.openMerge(mergeId);
    if (m_merge.isOpen())
        setPage(QStringLiteral("merge"));
}

// --------------------------------------------------------------------------- //
// Sistema operacional
// --------------------------------------------------------------------------- //

void AppController::revealInFolder(const QString &path)
{
    const QString native = QDir::toNativeSeparators(QDir::fromNativeSeparators(path));
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), native});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + native});
#else
    const QFileInfo fi(QDir::fromNativeSeparators(path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
#endif
}

void AppController::openLogsFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::logsDir()));
}

void AppController::openUrl(const QUrl &url)
{
    // Só links da web vão para o navegador do sistema.
    if (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
        QDesktopServices::openUrl(url);
}

void AppController::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

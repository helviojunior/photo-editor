// SPDX-License-Identifier: BSD-2-Clause
//
// PhotoEditor — editor de fotos de eventos (C++/Qt Quick).
//
//   PhotoEditor [PROJETO] [--debug] [--screenshot ARQUIVO]
//
// --screenshot abre o app, espera a primeira tela carregar, salva a imagem da
// janela e sai: teste de fumaça sem ninguém olhando.

#include "AppController.h"
#include "ImageProviders.h"
#include "Language.h"
#include "Logging.h"

#include "core/Paths.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QThreadPool>
#include <QTimer>

Q_LOGGING_CATEGORY(lcMain, "photoeditor.main")

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Nome e organização decidem onde o QSettings guarda as preferências.
    QGuiApplication::setApplicationName(QStringLiteral("PhotoEditor"));
    QGuiApplication::setOrganizationName(QStringLiteral("PhotoE"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("photoe.com.br"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PHOTOEDITOR_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral(PHOTOEDITOR_BRAND_NAME));
    QGuiApplication::setDesktopFileName(QStringLiteral("br.com.photoeditor.desktop"));
#ifndef Q_OS_MACOS
    // No macOS o Dock usa o ícone do pacote (PhotoEditor.icns); definir outro
    // aqui o trocaria enquanto o app roda. favicon.png = o mesmo desenho do
    // packaging/art.py (atualize com `make art` se o logo mudar).
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/PhotoEditor/assets/favicon.png")));
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Event photo editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project folder to open (the one with raw/)."));
    const QCommandLineOption debugOption(QStringLiteral("debug"), QStringLiteral("Verbose logging."));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              QStringLiteral("Save a screenshot of the first screen and quit."),
                                              QStringLiteral("file"));
    parser.addOptions({debugOption, screenshotOption});
    // O Finder às vezes passa -psn_0_12345: ignorado.
    QStringList args = QCoreApplication::arguments();
    args.erase(std::remove_if(args.begin(), args.end(), [](const QString &a) { return a.startsWith(QLatin1String("-psn_")); }),
               args.end());
    parser.process(args);

    pe::app::setupLogging(parser.isSet(debugOption));
    qCInfo(lcMain) << "PhotoEditor" << PHOTOEDITOR_VERSION << "starting; data in" << pe::AppPaths::dataDir();

    pe::app::installLanguage(pe::app::savedLanguage());

    // Inter vai no próprio binário: nada da interface depende de rede.
    for (const char *weight : {"Light", "Regular", "Medium", "SemiBold", "Bold"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/PhotoEditor/fonts/Inter-%1.ttf").arg(QLatin1String(weight)));
    QFont font(QStringLiteral("Inter"));
    font.setPixelSize(14);
    QGuiApplication::setFont(font);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller(nullptr);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("photo"), new PhotoImageProvider);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    QObject::connect(&controller, &AppController::languageChanged, &engine, &QQmlEngine::retranslate);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("PhotoEditor", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty())
        QTimer::singleShot(0, &controller, [&controller, path = positional.first()]() {
            controller.openProject(QDir(path).absolutePath());
        });

    if (parser.isSet(screenshotOption)) {
        const QString file = parser.value(screenshotOption);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        // Tempo para as imagens assíncronas chegarem; o timeout fecha mesmo se travar.
        QTimer::singleShot(positional.isEmpty() ? 1500 : 5000, window, [window, file]() {
            const QImage shot = window->grabWindow();
            const bool ok = shot.save(file);
            qCInfo(lcMain) << "Screenshot" << (ok ? "saved to" : "FAILED:") << file;
            QCoreApplication::exit(ok ? 0 : 2);
        });
        QTimer::singleShot(120000, &app, []() { QCoreApplication::exit(3); });
    }

    const int code = QGuiApplication::exec();
    QThreadPool::globalInstance()->waitForDone();
    qCInfo(lcMain) << "PhotoEditor exiting with" << code;
    return code;
}

// SPDX-FileCopyrightText: 2017 Konstantinos Sideris <siderisk@auth.gr>
// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>

#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QPoint>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include "ChatPage.h"
#include "Config.h"
#include "Logging.h"
#include "MainWindow.h"
#include "MatrixClient.h"
#include "Utils.h"
#include "config/nheko.h"

#ifndef Q_OS_ANDROID
#include "singleapplication.h"
#endif

#if defined(Q_OS_MAC)
#include "emoji/MacHelper.h"
#endif

#ifdef QML_DEBUGGING
#include <QQmlDebuggingEnabler>
QQmlDebuggingEnabler enabler;
#endif

#if HAVE_BACKTRACE_SYMBOLS_FD
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

void
stacktraceHandler(int signum)
{
        std::signal(signum, SIG_DFL);

        // boost::stacktrace::safe_dump_to("./nheko-backtrace.dump");

        // see
        // https://stackoverflow.com/questions/77005/how-to-automatically-generate-a-stacktrace-when-my-program-crashes/77336#77336
        void *array[50];
        size_t size;

        // get void*'s for all entries on the stack
        size = backtrace(array, 50);

        // print out all the frames to stderr
        fprintf(stderr, "Error: signal %d:\n", signum);
        backtrace_symbols_fd(array, size, STDERR_FILENO);

        int file = ::open("/tmp/nheko-crash.dump",
                          O_CREAT | O_WRONLY | O_TRUNC
#if defined(S_IWUSR) && defined(S_IRUSR)
                          ,
                          S_IWUSR | S_IRUSR
#elif defined(S_IWRITE) && defined(S_IREAD)
                          ,
                          S_IWRITE | S_IREAD
#endif
        );
        if (file != -1) {
                constexpr char header[]   = "Error: signal\n";
                [[maybe_unused]] auto ret = write(file, header, std::size(header) - 1);
                backtrace_symbols_fd(array, size, file);
                close(file);
        }

        std::raise(SIGABRT);
}

void
registerSignalHandlers()
{
        std::signal(SIGSEGV, &stacktraceHandler);
        std::signal(SIGABRT, &stacktraceHandler);
}

#else

// No implementation for systems with no stacktrace support.
void
registerSignalHandlers()
{}

#endif

QPoint
screenCenter(int width, int height)
{
        // Deprecated in 5.13: QRect screenGeometry = QApplication::desktop()->screenGeometry();
        QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();

        int x = (screenGeometry.width() - width) / 2;
        int y = (screenGeometry.height() - height) / 2;

        return QPoint(x, y);
}

void
createStandardDirectory(QStandardPaths::StandardLocation path)
{
        auto dir = QStandardPaths::writableLocation(path);

        if (!QDir().mkpath(dir)) {
                throw std::runtime_error(
                  ("Unable to create state directory:" + dir).toStdString().c_str());
        }
}

int
main(int argc, char *argv[])
{
        QCoreApplication::setApplicationName("nheko");
        QCoreApplication::setApplicationVersion(nheko::version);
        QCoreApplication::setOrganizationName("nheko");
        QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

        // this needs to be after setting the application name. Or how would we find our settings
        // file then?
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_FREEBSD)
        if (qgetenv("QT_SCALE_FACTOR").size() == 0) {
                float factor = utils::scaleFactor();

                if (factor != -1)
                        qputenv("QT_SCALE_FACTOR", QString::number(factor).toUtf8());
        }
#endif

        // This is some hacky programming, but it's necessary (AFAIK?) to get the unique config name
        // parsed before the SingleApplication userdata is set.
        QString userdata{""};
        QString matrixUri;
        for (int i = 1; i < argc; ++i) {
                QString arg{argv[i]};
                if (arg.startsWith("--profile=")) {
                        arg.remove("--profile=");
                        userdata = arg;
                } else if (arg.startsWith("--p=")) {
                        arg.remove("-p=");
                        userdata = arg;
                } else if (arg == "--profile" || arg == "-p") {
                        if (i < argc - 1) // if i is less than argc - 1, we still have a parameter
                                          // left to process as the name
                        {
                                ++i; // the next arg is the name, so increment
                                userdata = QString{argv[i]};
                        }
                } else if (arg.startsWith("matrix:")) {
                        matrixUri = arg;
                }
        }

#ifdef Q_OS_ANDROID
        QApplication app(argc, argv);
#else
        SingleApplication app(argc,
                              argv,
                              true,
                              SingleApplication::Mode::User |
                                SingleApplication::Mode::ExcludeAppPath |
                                SingleApplication::Mode::ExcludeAppVersion |
                                SingleApplication::Mode::SecondaryNotification,
                              100,
                              userdata);

        if (app.isSecondary()) {
                // open uri in main instance
                app.sendMessage(matrixUri.toUtf8());
                return 0;
        }
#endif

        QCommandLineParser parser;
        parser.addHelpOption();
        parser.addVersionOption();
        QCommandLineOption debugOption("debug", "Enable debug output");
        parser.addOption(debugOption);

        // This option is not actually parsed via Qt due to the need to parse it before the app
        // name is set. It only exists to keep Qt from complaining about the --profile/-p
        // option and thereby crashing the app.
        QCommandLineOption configName(
          QStringList() << "p"
                        << "profile",
          QCoreApplication::tr("Create a unique profile, which allows you to log into several "
                               "accounts at the same time and start multiple instances of nheko."),
          QCoreApplication::tr("profile"),
          QCoreApplication::tr("profile name"));
        parser.addOption(configName);

        parser.process(app);

        app.setWindowIcon(QIcon::fromTheme("nheko", QIcon{":/logos/nheko.png"}));

        http::init();

        createStandardDirectory(QStandardPaths::CacheLocation);
        createStandardDirectory(QStandardPaths::AppDataLocation);

        registerSignalHandlers();

        if (parser.isSet(debugOption))
                nhlog::enable_debug_log_from_commandline = true;

        try {
                nhlog::init(QString("%1/nheko.log")
                              .arg(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                              .toStdString());
        } catch (const spdlog::spdlog_ex &ex) {
                std::cout << "Log initialization failed: " << ex.what() << std::endl;
                std::exit(1);
        }

        if (parser.isSet(configName))
                UserSettings::initialize(parser.value(configName));
        else
                UserSettings::initialize(std::nullopt);

        auto settings = UserSettings::instance().toWeakRef();

        QFont font;
        QString userFontFamily = settings.lock()->font();
        if (!userFontFamily.isEmpty() && userFontFamily != "default") {
                font.setFamily(userFontFamily);
        }
        font.setPointSizeF(settings.lock()->fontSize());

        app.setFont(font);

        QString lang = QLocale::system().name();

        QTranslator qtTranslator;
        qtTranslator.load(
          QLocale(), "qt", "_", QLibraryInfo::location(QLibraryInfo::TranslationsPath));
        app.installTranslator(&qtTranslator);

        QTranslator appTranslator;
        appTranslator.load(QLocale(), "nheko", "_", ":/translations");
        app.installTranslator(&appTranslator);

        MainWindow w;

        // Move the MainWindow to the center
        w.move(screenCenter(w.width(), w.height()));

        if (!(settings.lock()->startInTray() && settings.lock()->tray()))
                w.show();

        QObject::connect(&app, &QApplication::aboutToQuit, &w, [&w]() {
                w.saveCurrentWindowSize();
                if (http::client() != nullptr) {
                        nhlog::net()->debug("shutting down all I/O threads & open connections");
                        http::client()->close(true);
                        nhlog::net()->debug("bye");
                }
        });

#ifndef Q_OS_ANDROID
        QObject::connect(&app, &SingleApplication::instanceStarted, &w, [&w]() {
                w.show();
                w.raise();
                w.activateWindow();
        });

        QObject::connect(
          &app,
          &SingleApplication::receivedMessage,
          ChatPage::instance(),
          [&](quint32, QByteArray message) { ChatPage::instance()->handleMatrixUri(message); });

        QMetaObject::Connection uriConnection;
        if (app.isPrimary() && !matrixUri.isEmpty()) {
                uriConnection = QObject::connect(ChatPage::instance(),
                                                 &ChatPage::contentLoaded,
                                                 ChatPage::instance(),
                                                 [&uriConnection, matrixUri]() {
                                                         ChatPage::instance()->handleMatrixUri(
                                                           matrixUri.toUtf8());
                                                         QObject::disconnect(uriConnection);
                                                 });
        }
#endif
        QDesktopServices::setUrlHandler("matrix", ChatPage::instance(), "handleMatrixUri");

#if defined(Q_OS_MAC)
        // Temporary solution for the emoji picker until
        // nheko has a proper menu bar with more functionality.
        MacHelper::initializeMenus();
#endif

        nhlog::ui()->info("starting nheko {}", nheko::version);

        return app.exec();
}

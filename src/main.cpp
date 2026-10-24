// Sanger — view Sanger traces, trim them, align them to a reference, and
// export their consensus.

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "alignmentview.h"
#include "backend.h"
#include "cli.h"
#include "theme.h"
#include "traceview.h"
#include "translator.h"

// The desktop's interface font, e.g. "Adwaita Sans 11", which the gtk3 platform
// theme used to supply. Without a settings portal Qt's default font stays.
// From Hype (MIT, David Heinemeier Hansson).
static void adoptDesktopFont() {
    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.Settings", "ReadOne");
    call.setArguments({"org.gnome.desktop.interface", "font-name"});
    const auto reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;
    const QString name = reply.arguments().first().value<QDBusVariant>().variant().toString();
    const int space = name.lastIndexOf(' ');
    const double size = name.mid(space + 1).toDouble();
    if (space <= 0 || size <= 0)
        return;
    QFont font(name.left(space));
    font.setPointSizeF(size);
    QGuiApplication::setFont(font);
}

int main(int argc, char *argv[]) {
    // Sanger themes itself, as Hype does. Qt's gtk3 platform theme only adds a
    // use-after-free inside GTK when the desktop theme changes under a running
    // app, and an accessibility-bus warning to every command.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");

    // Commands and help draw no window, so they must not need a display, even
    // where the desktop exports QT_QPA_PLATFORM=wayland. Bare sanger prints
    // help, as a command line tool should; the launcher says sanger open.
    const bool command = argc == 1 || isCliCommand(argv[1]);
    bool windowless = command;
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        windowless = windowless || argument == "--help" || argument == "-h" || argument == "--version" ||
                     argument == "-v";
    }
    if (windowless)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("sanger");
    app.setApplicationVersion("0.1.0");

    // Matches the window to sanger.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("sanger");
    app.setWindowIcon(QIcon::fromTheme("sanger"));

    if (command)
        return runCli(app.arguments());

    QCommandLineParser args;
    args.setApplicationDescription("View Sanger traces, trim them, align them to a reference, and export the "
                                   "consensus.\n\n" + cliSummary());
    args.addHelpOption();
    args.addVersionOption();
    args.addPositionalArgument("files", "AB1 or SCF traces and a FASTA reference", "[files...]");
    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);
    args.process(arguments);

    adoptDesktopFont();
    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    qmlRegisterUncreatableType<Backend>("Sanger", 1, 0, "Backend", "The backend comes from C++.");
    qmlRegisterType<AlignmentView>("Sanger", 1, 0, "AlignmentView");
    qmlRegisterType<TraceView>("Sanger", 1, 0, "TraceView");

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (!args.positionalArguments().isEmpty())
        backend.loadPaths(args.positionalArguments());

    return app.exec();
}

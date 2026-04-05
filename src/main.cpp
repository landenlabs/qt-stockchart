#include <QApplication>
#include "MainWindow.h"
#include "CrashHandler.h"
#include "Logger.h"
#include <cstdio>

static void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    fprintf(stdout, "%s\n", qPrintable(msg));
    fflush(stdout);
    if (type == QtInfoMsg || type == QtWarningMsg || type == QtCriticalMsg) {
        QMetaObject::invokeMethod(&Logger::instance(), "appendSilent",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, msg));
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("StockChart");
    app.setApplicationName("StockChart");
    app.setApplicationVersion("1.1.0");

    CrashHandler::install(); // must be after QApplication is constructed
    qInstallMessageHandler(qtMessageHandler);

    MainWindow window;
    window.show();

    return app.exec();
}

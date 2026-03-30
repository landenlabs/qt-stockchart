#include <QApplication>
#include "MainWindow.h"
#include "CrashHandler.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("StockChart");
    app.setApplicationVersion("1.1.0");

    CrashHandler::install(); // must be after QApplication is constructed

    MainWindow window;
    window.show();

    return app.exec();
}

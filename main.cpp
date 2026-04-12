#include <QApplication>

#include "popupwidget.h"
#include "stylusmonitor.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("stylus-popup");
    app.setQuitOnLastWindowClosed(false); // keep running even with no visible window

    PopupWidget  popup;
    StylusMonitor monitor;

    QObject::connect(&monitor, &StylusMonitor::stateChanged,
                     &popup,   &PopupWidget::showState,
                     Qt::QueuedConnection);

    monitor.start();
    return app.exec();
}

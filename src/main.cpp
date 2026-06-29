#include <QApplication>

#include "bluezmanager.h"
#include "popupwidget.h"
#include "stylusmonitor.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("stylus-popup");
    app.setQuitOnLastWindowClosed(false); // keep running even with no visible window

    PopupWidget   popup;
    BluezManager  bluez;
    StylusMonitor monitor;

    bool pairingRequested = false;

    auto onStateChanged = [&](const StylusState &state) {
        popup.showState(state);

        if (!state.attached || state.phase == StylusPhase::Attaching)
            pairingRequested = false;

        if (state.attached && state.macValid &&
            state.phase == StylusPhase::Complete && !pairingRequested) {
            bluez.ensurePaired(state.macAddress);
            pairingRequested = true;
        }
    };

    QObject::connect(&monitor, &StylusMonitor::stateChanged,
                     &app, onStateChanged,
                     Qt::QueuedConnection);

    QObject::connect(&bluez, &BluezManager::pairedAndConnected,
                     &popup, &PopupWidget::onBtConnected,
                     Qt::QueuedConnection);
    QObject::connect(&bluez, &BluezManager::pairingFailed,
                     &popup, &PopupWidget::onBtConnectionFailed,
                     Qt::QueuedConnection);

    monitor.start();
    return app.exec();
}

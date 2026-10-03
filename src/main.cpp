#include <QApplication>

#include "bluezmanager.h"
#include "popupwidget.h"
#include "stylusbuttons.h"
#include "stylusmonitor.h"

int main(int argc, char *argv[])
{
    /* Diagnostic that needs neither a Wayland session nor the popup: report
     * which input nodes the pen button mapping would grab. */
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--list-input") == 0) {
            StylusButtonMonitor::printMatchingDevices(ButtonMapConfig::load());
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("stylus-popup");
    app.setQuitOnLastWindowClosed(false); // keep running even with no visible window

    PopupWidget       popup;
    BluezManager      bluez;
    StylusMonitor     monitor;
    StylusButtonMapper buttons;

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

    buttons.start();
    monitor.start();
    return app.exec();
}

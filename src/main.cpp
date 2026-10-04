#include <QApplication>

#include "appconfig.h"
#include "bluezmanager.h"
#include "popupwidget.h"
#include "stylusbuttons.h"
#include "stylusmonitor.h"

int main(int argc, char *argv[])
{
    /* The config file is read exactly once, here. Each part of the program is
     * handed its own section below, so there is one reader and one place to
     * extend when a setting is added. */
    const AppConfig config = AppConfig::load();

    /* Diagnostic that needs neither a Wayland session nor the popup: report
     * which input nodes the pen button mapping would grab. */
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--list-input") == 0) {
            StylusButtonMonitor::printMatchingDevices(config.buttons, config.sourcePath);
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("stylus-popup");
    app.setQuitOnLastWindowClosed(false); // keep running even with no visible window

    PopupWidget       popup(config.popup);
    BluezManager      bluez;
    StylusMonitor     monitor;
    StylusButtonMapper buttons(config.buttons);

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

    QObject::connect(&bluez, &BluezManager::attemptStarted,
                     &popup, &PopupWidget::onConnectAttemptStarted,
                     Qt::QueuedConnection);
    QObject::connect(&bluez, &BluezManager::pairedAndConnected,
                     &popup, &PopupWidget::onBtConnected,
                     Qt::QueuedConnection);
    QObject::connect(&bluez, &BluezManager::pairingFailed,
                     &popup, &PopupWidget::onBtConnectionFailed,
                     Qt::QueuedConnection);

    /* The popup gives up on its own timer; the attempt behind it has to be
     * dropped as well, or its reply would arrive for a pen nobody waits for. */
    QObject::connect(&popup, &PopupWidget::connectTimedOut,
                     &bluez, &BluezManager::cancel);

    buttons.start();
    monitor.start();
    return app.exec();
}

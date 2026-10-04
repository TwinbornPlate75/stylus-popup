#include "appconfig.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {

constexpr char kButtonsGroup[] = "buttons";
constexpr char kPopupGroup[]   = "popup";

/* Gesture thresholds are clamped: a typo must not make the mapping unusable
 * (0 ms would let every click be swallowed as a long press, a huge value would
 * delay every click into the next session). */
constexpr int kMinGestureMs = 50;
constexpr int kMaxGestureMs = 10000;

/**
 * Reads a config value as text. QSettings hands back a QStringList as soon as
 * the value contains a comma, and toString() on such a value is empty - which
 * would silently drop a command like "foo --a,b". Re-joining the list
 * restores the text verbatim.
 */
QString iniText(const QSettings &settings, const QString &key, const QString &fallback)
{
    const QVariant value = settings.value(key);
    if (!value.isValid())
        return fallback;

    const QStringList parts = value.toStringList();
    return parts.size() > 1 ? parts.join(QLatin1Char(',')) : value.toString();
}

/** Reads a value in ms, clamped to [min, max]; an unparsable one keeps `fallback`. */
int iniMilliseconds(const QSettings &settings, const QString &key, int fallback, int min, int max)
{
    bool ok = false;
    const int value = settings.value(key).toInt(&ok);
    return ok ? qBound(min, value, max) : fallback;
}

/** ~/.config/stylus-popup/config.ini, or $STYLUS_POPUP_CONFIG when set. */
QString configPath()
{
    const QByteArray override = qgetenv("STYLUS_POPUP_CONFIG");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);

    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/stylus-popup/config.ini");
}

/**
 * Writes the shipped defaults with a short explanation, so the settings can be
 * discovered and edited without reading the source. This is the only writer:
 * the file is created once, then only ever read back.
 */
bool writeDefaultConfig(const AppConfig &config)
{
    const QFileInfo info(config.sourcePath);
    if (!QDir().mkpath(info.absolutePath())) {
        qWarning("AppConfig: cannot create %s", qPrintable(info.absolutePath()));
        return false;
    }

    QFile file(config.sourcePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("AppConfig: cannot write %s: %s",
                 qPrintable(config.sourcePath), qPrintable(file.errorString()));
        return false;
    }

    const ButtonMapConfig &buttons = config.buttons;

    QTextStream out(&file);
    out << "# stylus-popup configuration.\n"
           "#\n"
           "# Both side buttons of the stylus arrive over Bluetooth HID as ordinary\n"
           "# keyboard keys: the main button sends PAGE_DOWN, the secondary one\n"
           "# PAGE_UP. They are named after the pen, not after the key, so\n"
           "# \"primary\" is the main button and \"secondary\" the other one.\n"
           "# stylus-popup grabs them so they no longer reach the focused window,\n"
           "# and runs the matching command through /bin/sh -c with STYLUS_BUTTON\n"
           "# (\"primary\" or \"secondary\") and STYLUS_GESTURE\n"
           "# (\"single\", \"double-click\" or \"long-press\") exported.\n"
           "#\n"
           "# Each button has three gestures: <button> is a single click,\n"
           "# <button>-double-click is a second click within double-click-ms, and\n"
           "# <button>-long-press is a press held for long-press-ms. One press\n"
           "# produces at most one gesture. An empty command disables that gesture.\n"
           "# Whether a click is the first of two can only be known once the click\n"
           "# window has passed, so every click pays double-click-ms.\n"
           "\n"
           "[buttons]\n"
           "# Set to false to leave the buttons alone.\n"
           "enabled=true\n"
           "# Grab the input node exclusively, swallowing the key stroke.\n"
           "grab=true\n"
           "# Also fire the mapped command on key auto-repeat while the button is\n"
           "# held: the repeat re-runs the gesture the hold resolved to.\n"
           "repeat=false\n"
           "# Click window, and how long a press has to be held.\n"
           "double-click-ms=" << buttons.doubleClickMs << "\n"
           "long-press-ms=" << buttons.longPressMs << "\n"
           "\n"
           "primary=" << buttons.primaryCommand << "\n"
           "primary-double-click=" << buttons.primaryDouble << "\n"
           "primary-long-press=" << buttons.primaryLong << "\n"
           "secondary=" << buttons.secondaryCommand << "\n"
           "secondary-double-click=" << buttons.secondaryDouble << "\n"
           "secondary-long-press=" << buttons.secondaryLong << "\n"
           "\n"
           "[popup]\n"
           "# How long the pen has to connect after it attaches. When the wait\n"
           "# runs out the popup slides away and the pending attempt is dropped\n"
           "# until the pen is attached again. 0 waits forever.\n"
           "connect-timeout-ms=" << PopupConfig::kDefaultConnectTimeoutMs << "\n";
    file.close();

    qInfo("AppConfig: wrote the default configuration to %s",
          qPrintable(config.sourcePath));
    return true;
}

}  // namespace

AppConfig AppConfig::load(const QString &path)
{
    AppConfig config;
    config.sourcePath = path.isEmpty() ? configPath() : path;

    if (QFileInfo::exists(config.sourcePath)) {
        QSettings settings(config.sourcePath, QSettings::IniFormat);

        settings.beginGroup(kButtonsGroup);
        config.buttons.enabled = settings.value("enabled", config.buttons.enabled).toBool();
        config.buttons.grab    = settings.value("grab",    config.buttons.grab).toBool();
        config.buttons.repeat  = settings.value("repeat",  config.buttons.repeat).toBool();
        config.buttons.doubleClickMs =
            iniMilliseconds(settings, "double-click-ms", config.buttons.doubleClickMs,
                            kMinGestureMs, kMaxGestureMs);
        config.buttons.longPressMs =
            iniMilliseconds(settings, "long-press-ms", config.buttons.longPressMs,
                            kMinGestureMs, kMaxGestureMs);
        config.buttons.primaryCommand   = iniText(settings, "primary", config.buttons.primaryCommand);
        config.buttons.primaryDouble    = iniText(settings, "primary-double-click", config.buttons.primaryDouble);
        config.buttons.primaryLong      = iniText(settings, "primary-long-press", config.buttons.primaryLong);
        config.buttons.secondaryCommand = iniText(settings, "secondary", config.buttons.secondaryCommand);
        config.buttons.secondaryDouble  = iniText(settings, "secondary-double-click", config.buttons.secondaryDouble);
        config.buttons.secondaryLong    = iniText(settings, "secondary-long-press", config.buttons.secondaryLong);
        settings.endGroup();

        settings.beginGroup(kPopupGroup);
        config.popup.connectTimeoutMs =
            iniMilliseconds(settings, "connect-timeout-ms", config.popup.connectTimeoutMs,
                            0, PopupConfig::kMaxConnectTimeoutMs);
        settings.endGroup();

        qInfo("AppConfig: loaded %s", qPrintable(config.sourcePath));
    } else if (!writeDefaultConfig(config)) {
        qWarning("AppConfig: falling back to the built-in defaults");
    }

    if (config.popup.connectTimeoutMs > 0)
        qInfo("AppConfig: the pen has %d ms to connect", config.popup.connectTimeoutMs);
    else
        qInfo("AppConfig: the pen has unlimited time to connect");

    return config;
}

#include "popupconfig.h"

#include <QDebug>
#include <QSettings>

namespace {

constexpr char kPopupGroup[] = "popup";

/** Reads a value in ms, clamped to 0..kMaxConnectTimeoutMs. */
int iniTimeout(const QSettings &settings, const QString &key, int fallback)
{
    bool ok = false;
    const int value = settings.value(key).toInt(&ok);
    return ok ? qBound(0, value, PopupConfig::kMaxConnectTimeoutMs) : fallback;
}

}  // namespace

PopupConfig PopupConfig::load(const QString &path)
{
    PopupConfig config;

    QSettings settings(path, QSettings::IniFormat);
    settings.beginGroup(kPopupGroup);
    config.connectTimeoutMs = iniTimeout(settings, "connect-timeout-ms", config.connectTimeoutMs);
    settings.endGroup();

    if (config.connectTimeoutMs > 0)
        qInfo("PopupConfig: the pen has %d ms to connect", config.connectTimeoutMs);
    else
        qInfo("PopupConfig: the pen has unlimited time to connect");

    return config;
}

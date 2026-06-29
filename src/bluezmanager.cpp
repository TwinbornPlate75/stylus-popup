#include "bluezmanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingReply>

static const char *BLUEZ_SERVICE      = "org.bluez";
static const char *OBJECT_MANAGER_IF  = "org.freedesktop.DBus.ObjectManager";
static const char *ADAPTER_IF         = "org.bluez.Adapter1";
static const char *DEVICE_IF          = "org.bluez.Device1";

BluezManager::BluezManager(QObject *parent)
    : QObject(parent)
    , m_discoveryTimer(new QTimer(this))
{
    m_discoveryTimer->setSingleShot(true);
    m_discoveryTimer->setInterval(kDiscoveryTimeoutMs);
    connect(m_discoveryTimer, &QTimer::timeout, this, &BluezManager::onDiscoveryTimeout);

    qDBusRegisterMetaType<QMap<QString, QVariantMap>>();
    qDBusRegisterMetaType<QMap<QDBusObjectPath, QMap<QString, QVariantMap>>>();

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.connect(BLUEZ_SERVICE, "/", OBJECT_MANAGER_IF, "InterfacesAdded",
                     this, SLOT(onInterfacesAdded(QDBusObjectPath,QMap<QString,QVariantMap>)))) {
        qWarning("BluezManager: failed to connect InterfacesAdded signal");
    }
}

void BluezManager::ensurePaired(const QString &macAddress)
{
    if (macAddress.isEmpty()) {
        qWarning("BluezManager: empty MAC address, skipping");
        return;
    }

    if (!m_pendingMac.isEmpty()) {
        qDebug("BluezManager: pairing already in progress for %s, ignoring %s",
               qPrintable(m_pendingMac), qPrintable(macAddress));
        return;
    }

    m_pendingMac = macAddress;

    QDBusInterface objectManager(BLUEZ_SERVICE, "/", OBJECT_MANAGER_IF,
                                 QDBusConnection::systemBus());
    if (!objectManager.isValid()) {
        qWarning("BluezManager: cannot connect to org.bluez ObjectManager: %s",
                 qPrintable(objectManager.lastError().message()));
        finish();
        return;
    }

    QDBusPendingCall call = objectManager.asyncCall("GetManagedObjects");
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &BluezManager::onManagedObjectsReply);
}

void BluezManager::onManagedObjectsReply(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();

    QDBusPendingReply<QMap<QDBusObjectPath, QMap<QString, QVariantMap>>> reply = *watcher;
    if (reply.isError()) {
        reportError("GetManagedObjects", reply.error());
        finish();
        return;
    }

    const auto objects = reply.value();
    QString adapterPath;
    QDBusObjectPath matchedDevice;
    bool alreadyPaired = false;
    bool deviceFound   = false;

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QDBusObjectPath &path = it.key();
        const QMap<QString, QVariantMap> &interfaces = it.value();

        if (interfaces.contains(ADAPTER_IF) && adapterPath.isEmpty())
            adapterPath = path.path();

        if (!interfaces.contains(DEVICE_IF))
            continue;

        const QVariantMap &props = interfaces.value(DEVICE_IF);
        const QString address = props.value("Address").toString();
        if (!isSameAddress(address, m_pendingMac))
            continue;

        deviceFound   = true;
        matchedDevice = path;
        alreadyPaired = props.value("Paired").toBool();
        break;
    }

    if (deviceFound) {
        if (alreadyPaired) {
            qInfo("BluezManager: device %s is already paired", qPrintable(m_pendingMac));
            emit pairedAndConnected(m_pendingMac);
            finish();
            return;
        }
        pairAndConnectDevice(matchedDevice);
        return;
    }

    qInfo("BluezManager: device %s not in managed objects, will discover",
          qPrintable(m_pendingMac));

    if (adapterPath.isEmpty()) {
        qWarning("BluezManager: no Bluetooth adapter found");
        finish();
        return;
    }

    m_activeAdapterPath = adapterPath;
    startDiscovery();
}

void BluezManager::startDiscovery()
{
    QDBusInterface adapter(BLUEZ_SERVICE, m_activeAdapterPath, ADAPTER_IF,
                           QDBusConnection::systemBus());
    if (!adapter.isValid()) {
        qWarning("BluezManager: adapter %s is not valid: %s",
                 qPrintable(m_activeAdapterPath),
                 qPrintable(adapter.lastError().message()));
        finish();
        return;
    }

    QDBusPendingCall call = adapter.asyncCall("StartDiscovery");
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &BluezManager::onStartDiscoveryReply);
}

void BluezManager::onStartDiscoveryReply(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();

    QDBusPendingReply<> reply = *watcher;
    if (reply.isError()) {
        reportError("StartDiscovery", reply.error());
        finish();
        return;
    }

    m_discovering = true;
    m_discoveryTimer->start();
    qInfo("BluezManager: started discovery for %s", qPrintable(m_pendingMac));
}

void BluezManager::onInterfacesAdded(const QDBusObjectPath &path,
                                     const QMap<QString, QVariantMap> &interfaces)
{
    if (m_pendingMac.isEmpty() || !m_discovering)
        return;

    if (!interfaces.contains(DEVICE_IF))
        return;

    const QVariantMap &props = interfaces.value(DEVICE_IF);
    const QString address = props.value("Address").toString();
    if (!isSameAddress(address, m_pendingMac))
        return;

    qInfo("BluezManager: discovered device %s at %s",
          qPrintable(m_pendingMac), qPrintable(path.path()));

    stopDiscovery();

    if (props.value("Paired").toBool()) {
        qInfo("BluezManager: device %s is already paired", qPrintable(m_pendingMac));
        emit pairedAndConnected(m_pendingMac);
        finish();
        return;
    }

    pairAndConnectDevice(path);
}

void BluezManager::stopDiscovery()
{
    m_discoveryTimer->stop();
    if (!m_discovering || m_activeAdapterPath.isEmpty())
        return;

    m_discovering = false;

    QDBusInterface adapter(BLUEZ_SERVICE, m_activeAdapterPath, ADAPTER_IF,
                           QDBusConnection::systemBus());
    if (!adapter.isValid())
        return;

    adapter.asyncCall("StopDiscovery");
}

void BluezManager::onDiscoveryTimeout()
{
    qWarning("BluezManager: discovery timed out for %s", qPrintable(m_pendingMac));
    emit pairingFailed(m_pendingMac, QStringLiteral("discovery timed out"));
    stopDiscovery();
    finish();
}

void BluezManager::pairAndConnectDevice(const QDBusObjectPath &devicePath)
{
    qInfo("BluezManager: pairing %s", qPrintable(m_pendingMac));

    QDBusInterface device(BLUEZ_SERVICE, devicePath.path(), DEVICE_IF,
                          QDBusConnection::systemBus());
    if (!device.isValid()) {
        qWarning("BluezManager: device interface is not valid: %s",
                 qPrintable(device.lastError().message()));
        finish();
        return;
    }

    QDBusPendingCall call = device.asyncCall("Pair");
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    watcher->setProperty("devicePath", devicePath.path());
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &BluezManager::onPairReply);
}

void BluezManager::onPairReply(QDBusPendingCallWatcher *watcher)
{
    const QString devicePath = watcher->property("devicePath").toString();
    watcher->deleteLater();

    QDBusPendingReply<> reply = *watcher;
    if (reply.isError()) {
        reportError("Pair", reply.error());
        emit pairingFailed(m_pendingMac, reply.error().message());
        finish();
        return;
    }

    qInfo("BluezManager: pair succeeded for %s, connecting", qPrintable(m_pendingMac));

    QDBusInterface device(BLUEZ_SERVICE, devicePath, DEVICE_IF,
                          QDBusConnection::systemBus());
    QDBusPendingCall call = device.asyncCall("Connect");
    auto *connectWatcher = new QDBusPendingCallWatcher(call, this);
    connect(connectWatcher, &QDBusPendingCallWatcher::finished,
            this, &BluezManager::onConnectReply);
}

void BluezManager::onConnectReply(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();

    QDBusPendingReply<> reply = *watcher;
    if (reply.isError()) {
        reportError("Connect", reply.error());
        emit pairingFailed(m_pendingMac, reply.error().message());
    } else {
        qInfo("BluezManager: connect succeeded for %s", qPrintable(m_pendingMac));
        emit pairedAndConnected(m_pendingMac);
    }

    finish();
}

void BluezManager::finish()
{
    stopDiscovery();
    m_activeAdapterPath.clear();
    m_pendingMac.clear();
}

void BluezManager::reportError(const QString &action, const QDBusError &error)
{
    qWarning("BluezManager: %s failed for %s: %s (%s)",
             qPrintable(action),
             qPrintable(m_pendingMac),
             qPrintable(error.name()),
             qPrintable(error.message()));
}

bool BluezManager::isSameAddress(const QString &a, const QString &b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

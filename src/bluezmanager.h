#pragma once

#include <QObject>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusError>
#include <QTimer>

class BluezManager : public QObject
{
    Q_OBJECT

public:
    explicit BluezManager(QObject *parent = nullptr);

    void ensurePaired(const QString &macAddress);

signals:
    void pairedAndConnected(const QString &macAddress);
    void pairingFailed(const QString &macAddress, const QString &error);

private slots:
    void onManagedObjectsReply(QDBusPendingCallWatcher *watcher);
    void onPairReply(QDBusPendingCallWatcher *watcher);
    void onConnectReply(QDBusPendingCallWatcher *watcher);
    void onStartDiscoveryReply(QDBusPendingCallWatcher *watcher);
    void onInterfacesAdded(const QDBusObjectPath &path,
                           const QMap<QString, QVariantMap> &interfaces);
    void onDiscoveryTimeout();

private:
    void pairAndConnectDevice(const QDBusObjectPath &devicePath);
    void startDiscovery();
    void stopDiscovery();
    void finish();
    void reportError(const QString &action, const QDBusError &error);

    static bool isSameAddress(const QString &a, const QString &b);

    static constexpr int kDiscoveryTimeoutMs = 15000;

    QString m_pendingMac;
    QString m_activeAdapterPath;
    bool    m_discovering = false;
    QTimer *m_discoveryTimer;
};


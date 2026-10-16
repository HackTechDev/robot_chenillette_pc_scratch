#pragma once

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QTimer>

// Agent BlueZ : répond au PIN demandé par le module (appairage « legacy »)
class Agent : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    using QObject::QObject;

signals:
    void message(const QString &text);

public slots:
    QString RequestPinCode(const QDBusObjectPath &device);
    uint RequestPasskey(const QDBusObjectPath &device);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void RequestAuthorization(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void Cancel();
    void Release();
};

// Appairage automatique du robot : allume l'adaptateur, cherche « BatMobile »,
// l'appaire avec le PIN 0000 et le marque de confiance. Ne fait rien s'il est déjà
// appairé (sauf repair). Émet succeeded ou failed une seule fois.
class Pairing : public QObject
{
    Q_OBJECT

public:
    Pairing(bool repair, int timeoutSec, QObject *parent = nullptr);
    ~Pairing() override;

    void start();

signals:
    void progress(const QString &message);
    void succeeded(const QString &address);
    void failed(const QString &error);

private slots:
    void onInterfacesAdded(const QDBusMessage &msg);
    void onPropertiesChanged(const QDBusMessage &msg);

private:
    QString findRobot(QVariantMap *device, QString *error) const;
    void discover();
    void stopDiscovery();
    void found(const QString &path, const QString &address);
    void pair(const QString &path);
    void onPaired(const QString &path);
    void onPairError(const QString &path, const QDBusError &e);
    bool trust(const QString &path, QString *error);
    void succeed(const QString &address);
    void fail(const QString &error);
    void cleanup();

    const bool repair_;
    const int timeout_;
    QDBusConnection bus_;
    Agent agent_;
    QTimer discoveryTimer_;
    QTimer overallTimer_;
    QString adapterPath_;
    QString pairingPath_;  // Pair() en cours sur cet appareil
    bool done_ = false;
    bool discovering_ = false;
    bool pairingStarted_ = false;
    bool signalsConnected_ = false;
    bool agentObjectRegistered_ = false;
    bool agentRegistered_ = false;
};

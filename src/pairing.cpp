#include "pairing.h"

#include "bluez.h"
#include "config.h"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

using namespace bluez;

namespace {
const QString AgentPath = QStringLiteral("/batmobile/agent");
constexpr int PairTimeoutMs = 60000;
}

// --- Agent ---

QString Agent::RequestPinCode(const QDBusObjectPath &)
{
    emit message(QStringLiteral("PIN %1 envoyé").arg(RobotPin));
    return QString::fromLatin1(RobotPin);
}

uint Agent::RequestPasskey(const QDBusObjectPath &)
{
    return QString::fromLatin1(RobotPin).toUInt();
}

void Agent::RequestConfirmation(const QDBusObjectPath &, uint) {}  // accepte
void Agent::AuthorizeService(const QDBusObjectPath &, const QString &) {}
void Agent::RequestAuthorization(const QDBusObjectPath &) {}
void Agent::DisplayPinCode(const QDBusObjectPath &, const QString &) {}
void Agent::DisplayPasskey(const QDBusObjectPath &, uint, ushort) {}

void Agent::Cancel()
{
    emit message(QStringLiteral("Appairage annulé par BlueZ"));
}

void Agent::Release() {}

// --- Pairing ---

Pairing::Pairing(bool repair, int timeoutSec, QObject *parent)
    : QObject(parent), repair_(repair), timeout_(timeoutSec), bus_(QDBusConnection::systemBus())
{
    registerTypes();
    connect(&agent_, &Agent::message, this, &Pairing::progress);
    discoveryTimer_.setSingleShot(true);
    connect(&discoveryTimer_, &QTimer::timeout, this, [this] {
        if (discovering_ && !pairingStarted_)
            fail(QStringLiteral("%1 introuvable après %2 s : robot allumé et à portée ?").arg(RobotName).arg(timeout_));
    });
    overallTimer_.setSingleShot(true);
    connect(&overallTimer_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Délai dépassé")); });
}

Pairing::~Pairing()
{
    cleanup();
}

void Pairing::start()
{
    if (!bus_.isConnected())
        return fail(QStringLiteral("Bus D-Bus système inaccessible : %1").arg(bus_.lastError().message()));

    QString error;
    const ManagedObjects objects = managedObjects(&error);
    if (!error.isEmpty())
        return fail(error);
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (it->contains(AdapterIface)) {
            adapterPath_ = it.key().path();
            break;
        }
    }
    if (adapterPath_.isEmpty())
        return fail(QStringLiteral("Aucun adaptateur Bluetooth trouvé"));

    const QVariant powered = bluez::property(adapterPath_, AdapterIface, QStringLiteral("Powered"), &error);
    if (!error.isEmpty())
        return fail(error);
    if (!powered.toBool()) {
        emit progress(QStringLiteral("Allumage de l'adaptateur Bluetooth"));
        if (!bluez::setProperty(adapterPath_, AdapterIface, QStringLiteral("Powered"), true, &error))
            return fail(error);
    }

    QVariantMap dev;
    QString path = findRobot(&dev, &error);
    if (!error.isEmpty())
        return fail(error);
    const QString address = dev.value(QStringLiteral("Address")).toString();

    if (!path.isEmpty() && repair_) {
        emit progress(QStringLiteral("Suppression de l'appairage existant (%1)").arg(address));
        const QDBusMessage reply = call(adapterPath_, AdapterIface, QStringLiteral("RemoveDevice"),
                                        {QVariant::fromValue(QDBusObjectPath(path))});
        if (reply.type() == QDBusMessage::ErrorMessage)
            return fail(errorText(reply));
        path.clear();
    }

    if (!path.isEmpty() && dev.value(QStringLiteral("Paired")).toBool()) {
        if (!trust(path, &error))
            return fail(error);
        emit progress(QStringLiteral("%1 déjà appairé (%2)").arg(RobotName, address));
        return succeed(address);
    }

    if (!bus_.registerObject(AgentPath, &agent_, QDBusConnection::ExportAllSlots))
        return fail(QStringLiteral("Impossible d'enregistrer l'agent D-Bus"));
    agentObjectRegistered_ = true;
    const QDBusMessage reply = call(QStringLiteral("/org/bluez"), AgentManagerIface, QStringLiteral("RegisterAgent"),
                                    {QVariant::fromValue(QDBusObjectPath(AgentPath)), QStringLiteral("KeyboardDisplay")});
    if (reply.type() == QDBusMessage::ErrorMessage)
        return fail(errorText(reply));
    agentRegistered_ = true;

    if (!path.isEmpty())
        pair(path);  // déjà vu par BlueZ : inutile de rechercher
    else
        discover();
    if (!done_)
        overallTimer_.start((timeout_ + 30) * 1000);
}

QString Pairing::findRobot(QVariantMap *device, QString *error) const
{
    const ManagedObjects objects = managedObjects(error);
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto dev = it->constFind(DeviceIface);
        if (dev != it->cend() && it.key().path().startsWith(adapterPath_ + '/') && isRobot(*dev)) {
            *device = *dev;
            return it.key().path();
        }
    }
    return {};
}

// --- Recherche ---

void Pairing::discover()
{
    emit progress(QStringLiteral("Recherche de %1… (robot allumé, à portée)").arg(RobotName));
    bus_.connect(Service, QStringLiteral("/"), ObjectManagerIface, QStringLiteral("InterfacesAdded"),
                 this, SLOT(onInterfacesAdded(QDBusMessage)));
    bus_.connect(Service, QString(), PropsIface, QStringLiteral("PropertiesChanged"),
                 this, SLOT(onPropertiesChanged(QDBusMessage)));
    signalsConnected_ = true;
    // Le Bluetooth Bee est en classique ; filtre facultatif, erreur ignorée
    call(adapterPath_, AdapterIface, QStringLiteral("SetDiscoveryFilter"),
         {QVariantMap{{QStringLiteral("Transport"), QStringLiteral("bredr")}}});
    const QDBusMessage reply = call(adapterPath_, AdapterIface, QStringLiteral("StartDiscovery"));
    if (reply.type() == QDBusMessage::ErrorMessage)
        return fail(errorText(reply));
    discovering_ = true;
    discoveryTimer_.start(timeout_ * 1000);
}

void Pairing::stopDiscovery()
{
    if (discovering_) {
        discovering_ = false;
        call(adapterPath_, AdapterIface, QStringLiteral("StopDiscovery"));
    }
}

void Pairing::onInterfacesAdded(const QDBusMessage &msg)
{
    const QString path = msg.arguments().value(0).value<QDBusObjectPath>().path();
    const InterfaceList ifaces = qdbus_cast<InterfaceList>(msg.arguments().value(1));
    const auto dev = ifaces.constFind(DeviceIface);
    if (dev != ifaces.cend() && isRobot(*dev))
        found(path, dev->value(QStringLiteral("Address")).toString());
}

void Pairing::onPropertiesChanged(const QDBusMessage &msg)
{
    // Le nom arrive parfois après la création de l'appareil
    if (msg.arguments().value(0).toString() != DeviceIface)
        return;
    const QVariantMap changed = qdbus_cast<QVariantMap>(msg.arguments().value(1));
    if (changed.value(QStringLiteral("Name")).toString() == QLatin1String(RobotName))
        found(msg.path(), QString());
}

void Pairing::found(const QString &path, const QString &address)
{
    if (pairingStarted_ || done_)
        return;
    emit progress(QStringLiteral("%1 trouvé").arg(RobotName)
                  + (address.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(address)));
    stopDiscovery();
    pair(path);
}

// --- Appairage ---

void Pairing::pair(const QString &path)
{
    pairingStarted_ = true;
    pairingPath_ = path;
    emit progress(QStringLiteral("Appairage (PIN %1)…").arg(RobotPin));
    const QDBusMessage msg = QDBusMessage::createMethodCall(Service, path, DeviceIface, QStringLiteral("Pair"));
    auto *watcher = new QDBusPendingCallWatcher(bus_.asyncCall(msg, PairTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        pairingPath_.clear();
        if (done_)
            return;
        const QDBusPendingReply<> reply = *w;
        if (reply.isError())
            onPairError(path, reply.error());
        else
            onPaired(path);
    });
}

void Pairing::onPaired(const QString &path)
{
    QString error;
    if (!trust(path, &error))
        return fail(error);
    const QString address = bluez::property(path, DeviceIface, QStringLiteral("Address"), &error).toString();
    if (!error.isEmpty())
        return fail(error);
    emit progress(QStringLiteral("%1 appairé (%2)").arg(RobotName, address));
    succeed(address);
}

void Pairing::onPairError(const QString &path, const QDBusError &e)
{
    const QString name = e.name();
    const QString message = e.message().isEmpty() ? name : e.message();
    if (name == QLatin1String("org.bluez.Error.AlreadyExists"))
        onPaired(path);
    else if (name == QLatin1String("org.bluez.Error.ConnectionAttemptFailed")
             || name == QLatin1String("org.bluez.Error.AuthenticationTimeout"))
        fail(QStringLiteral("Robot injoignable (%1) : robot allumé et à portée ?").arg(message));
    else if (name == QLatin1String("org.bluez.Error.AuthenticationFailed")
             || name == QLatin1String("org.bluez.Error.AuthenticationRejected"))
        fail(QStringLiteral("PIN refusé (%1) : réessayer avec --repair").arg(message));
    else
        fail(QStringLiteral("Échec de l'appairage : %1").arg(message));
}

bool Pairing::trust(const QString &path, QString *error)
{
    const QVariant trusted = bluez::property(path, DeviceIface, QStringLiteral("Trusted"), error);
    if (!error->isEmpty())
        return false;
    if (!trusted.toBool()) {
        if (!bluez::setProperty(path, DeviceIface, QStringLiteral("Trusted"), true, error))
            return false;
        emit progress(QStringLiteral("%1 marqué de confiance").arg(RobotName));
    }
    return true;
}

void Pairing::succeed(const QString &address)
{
    if (done_)
        return;
    done_ = true;
    cleanup();
    emit succeeded(address);
}

void Pairing::fail(const QString &error)
{
    if (done_)
        return;
    done_ = true;
    cleanup();
    emit failed(error);
}

void Pairing::cleanup()
{
    discoveryTimer_.stop();
    overallTimer_.stop();
    if (signalsConnected_) {
        signalsConnected_ = false;
        bus_.disconnect(Service, QStringLiteral("/"), ObjectManagerIface, QStringLiteral("InterfacesAdded"),
                        this, SLOT(onInterfacesAdded(QDBusMessage)));
        bus_.disconnect(Service, QString(), PropsIface, QStringLiteral("PropertiesChanged"),
                        this, SLOT(onPropertiesChanged(QDBusMessage)));
    }
    stopDiscovery();
    if (!pairingPath_.isEmpty()) {
        // Abandon en cours d'appairage (fermeture de l'appli, délai dépassé)
        bus_.send(QDBusMessage::createMethodCall(Service, pairingPath_, DeviceIface, QStringLiteral("CancelPairing")));
        pairingPath_.clear();
    }
    if (agentRegistered_) {
        agentRegistered_ = false;
        call(QStringLiteral("/org/bluez"), AgentManagerIface, QStringLiteral("UnregisterAgent"),
             {QVariant::fromValue(QDBusObjectPath(AgentPath))});
    }
    if (agentObjectRegistered_) {
        agentObjectRegistered_ = false;
        bus_.unregisterObject(AgentPath);
    }
}

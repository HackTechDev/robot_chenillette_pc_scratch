#include "bluez.h"

#include "config.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>

namespace bluez {

void registerTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<InterfaceList>();
        qDBusRegisterMetaType<ManagedObjects>();
        return true;
    }();
    Q_UNUSED(registered);
}

QDBusMessage call(const QString &path, const QString &iface, const QString &method,
                  const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, path, iface, method);
    msg.setArguments(args);
    return QDBusConnection::systemBus().call(msg);
}

QString errorText(const QDBusMessage &reply)
{
    return reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
}

ManagedObjects managedObjects(QString *error)
{
    registerTypes();
    const QDBusMessage reply = call(QStringLiteral("/"), ObjectManagerIface, QStringLiteral("GetManagedObjects"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = errorText(reply);
        return {};
    }
    error->clear();
    return qdbus_cast<ManagedObjects>(reply.arguments().value(0));
}

QVariant property(const QString &path, const QString &iface, const QString &name, QString *error)
{
    const QDBusMessage reply = call(path, PropsIface, QStringLiteral("Get"), {iface, name});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = errorText(reply);
        return {};
    }
    error->clear();
    return reply.arguments().value(0).value<QDBusVariant>().variant();
}

bool setProperty(const QString &path, const QString &iface, const QString &name,
                 const QVariant &value, QString *error)
{
    const QDBusMessage reply = call(path, PropsIface, QStringLiteral("Set"),
                                    {iface, name, QVariant::fromValue(QDBusVariant(value))});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = errorText(reply);
        return false;
    }
    return true;
}

QString deviceName(const QVariantMap &device)
{
    const QString alias = device.value(QStringLiteral("Alias")).toString();
    return alias.isEmpty() ? device.value(QStringLiteral("Name")).toString() : alias;
}

bool isRobot(const QVariantMap &device)
{
    return device.value(QStringLiteral("Name")).toString() == QLatin1String(RobotName)
        || device.value(QStringLiteral("Alias")).toString() == QLatin1String(RobotName);
}

QList<Device> pairedDevices()
{
    QString error;
    const ManagedObjects objects = managedObjects(&error);
    QList<Device> devices;
    for (const InterfaceList &ifaces : objects) {
        const auto dev = ifaces.constFind(DeviceIface);
        if (dev == ifaces.cend() || !dev->value(QStringLiteral("Paired")).toBool())
            continue;
        devices.append({dev->value(QStringLiteral("Address")).toString(), deviceName(*dev)});
    }
    return devices;
}

}  // namespace bluez

#pragma once

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariantMap>

// Accès à BlueZ par D-Bus (bus système)
namespace bluez {

inline const QString Service = QStringLiteral("org.bluez");
inline const QString AdapterIface = QStringLiteral("org.bluez.Adapter1");
inline const QString DeviceIface = QStringLiteral("org.bluez.Device1");
inline const QString AgentManagerIface = QStringLiteral("org.bluez.AgentManager1");
inline const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");
inline const QString ObjectManagerIface = QStringLiteral("org.freedesktop.DBus.ObjectManager");

using InterfaceList = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceList>;

struct Device {
    QString address;
    QString name;
};

void registerTypes();

// Appel bloquant ; en cas d'erreur, le message retourné est de type ErrorMessage
QDBusMessage call(const QString &path, const QString &iface, const QString &method,
                  const QVariantList &args = {});
QString errorText(const QDBusMessage &reply);

ManagedObjects managedObjects(QString *error);
QVariant property(const QString &path, const QString &iface, const QString &name, QString *error);
bool setProperty(const QString &path, const QString &iface, const QString &name,
                 const QVariant &value, QString *error);

// Nom affiché (Alias, à défaut Name) et reconnaissance du robot par son nom
QString deviceName(const QVariantMap &device);
bool isRobot(const QVariantMap &device);

// Appareils appairés (adresse, nom), dans l'ordre de BlueZ
QList<Device> pairedDevices();

}  // namespace bluez

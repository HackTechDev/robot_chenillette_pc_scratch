#include "link.h"

#include "config.h"

#include <QSocketNotifier>
#include <QStringList>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// Équivalents de <bluetooth/rfcomm.h> (libbluetooth-dev inutile)
constexpr int BtProtoRfcomm = 3;

struct SockAddrRc {
    sa_family_t rc_family;
    uint8_t rc_bdaddr[6];  // octets dans l'ordre inverse de l'écriture « 00:18:A1:… »
    uint8_t rc_channel;
};

constexpr int WriteTimeoutMs = 1000;

bool parseAddress(const QString &text, uint8_t out[6])
{
    const QStringList parts = text.split(':');
    if (parts.size() != 6)
        return false;
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const uint value = parts[i].toUInt(&ok, 16);
        if (!ok || parts[i].size() != 2)
            return false;
        out[5 - i] = uint8_t(value);
    }
    return true;
}

QString errnoText(int err)
{
    return QStringLiteral("[Errno %1] %2").arg(err).arg(QString::fromLocal8Bit(std::strerror(err)));
}

}  // namespace

Link::~Link()
{
    disconnectFromRobot();
}

void Link::connectTo(const QString &address, char firstCmd)
{
    closeSocket();
    SockAddrRc addr{};
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = RfcommChannel;
    if (!parseAddress(address, addr.rc_bdaddr))
        return failLater(QStringLiteral("Adresse Bluetooth invalide : %1").arg(address));

    const int fd = ::socket(AF_BLUETOOTH, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, BtProtoRfcomm);
    if (fd < 0)
        return failLater(errnoText(errno));
    fd_ = fd;
    connecting_ = true;
    firstCmd_ = firstCmd;
    address_ = address;

    if (::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof addr) == 0) {
        QMetaObject::invokeMethod(this, &Link::finishConnect, Qt::QueuedConnection);
        return;
    }
    if (errno != EINPROGRESS) {
        const int err = errno;
        closeSocket();
        return failLater(errnoText(err));
    }
    // Connexion en cours : la socket devient inscriptible quand elle aboutit ou échoue
    notifier_ = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    connect(notifier_, &QSocketNotifier::activated, this, &Link::finishConnect);
}

void Link::finishConnect()
{
    if (!connecting_)
        return;
    connecting_ = false;
    dropNotifier();

    int err = 0;
    socklen_t len = sizeof err;
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
        err = errno;
    // Synchronise la vitesse du robot (réinitialisée à chaque démarrage)
    if (err == 0)
        err = writeByte(firstCmd_);
    if (err != 0) {
        closeSocket();
        emit failed(errnoText(err));
        return;
    }
    notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &Link::onReadable);
    emit connected(address_);
}

void Link::onReadable()
{
    char buf[64];
    const ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
    if (n > 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)))
        return;
    // Fin de flux ou erreur : liaison perdue
    closeSocket();
    emit lost();
}

int Link::writeByte(char c)
{
    for (bool retried = false;;) {
        if (::send(fd_, &c, 1, MSG_NOSIGNAL) == 1)
            return 0;
        const int err = errno;
        if (err == EINTR)
            continue;
        if ((err != EAGAIN && err != EWOULDBLOCK) || retried)
            return err;
        // Tampon d'émission plein : on attend un peu, comme un envoi bloquant
        pollfd p{fd_, POLLOUT, 0};
        ::poll(&p, 1, WriteTimeoutMs);
        retried = true;
    }
}

void Link::send(char cmd)
{
    if (fd_ < 0 || connecting_)
        return;
    if (writeByte(cmd) != 0) {
        closeSocket();
        emit lost();
    }
}

void Link::disconnectFromRobot()
{
    if (fd_ < 0)
        return;
    if (!connecting_)
        writeByte(Stop);  // ne pas laisser le robot rouler
    ::shutdown(fd_, SHUT_RDWR);
    closeSocket();
}

void Link::failLater(const QString &error)
{
    QMetaObject::invokeMethod(this, [this, error] { emit failed(error); }, Qt::QueuedConnection);
}

void Link::closeSocket()
{
    dropNotifier();
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
    connecting_ = false;
}

void Link::dropNotifier()
{
    if (notifier_) {
        notifier_->setEnabled(false);
        notifier_->deleteLater();  // peut être appelé depuis son propre signal
        notifier_ = nullptr;
    }
}

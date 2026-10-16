#pragma once

#include <QObject>
#include <QString>

class QSocketNotifier;

// Liaison RFCOMM avec le robot : socket Bluetooth Linux non bloquant, suivi par la boucle
// d'événements Qt (pas de thread). Le robot n'envoie rien : la socket ne devient lisible
// qu'en cas de fermeture ou d'erreur de liaison, ce qui déclenche lost().
class Link : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~Link() override;

    // Connexion asynchrone ; firstCmd est envoyé dès la connexion (vitesse courante)
    void connectTo(const QString &address, char firstCmd);
    void send(char cmd);
    // Envoie un stop avant de fermer
    void disconnectFromRobot();

signals:
    void connected(const QString &address);
    void failed(const QString &error);
    void lost();

private:
    void finishConnect();
    void onReadable();
    int writeByte(char c);  // 0 ou errno
    void failLater(const QString &error);
    void closeSocket();
    void dropNotifier();

    int fd_ = -1;
    bool connecting_ = false;
    char firstCmd_ = 0;
    QString address_;
    QSocketNotifier *notifier_ = nullptr;
};

#pragma once

#include "blocks.h"

#include <QList>
#include <QObject>
#include <QTimer>

class Link;

// Exécute un script pas à pas dans la boucle d'événements (pas de thread). Un bloc
// « pendant N s » envoie sa commande, attend N s puis envoie un stop : même protocole
// qu'un bouton maintenu puis relâché.
//
// Pas à pas : le programme s'arrête avant chaque bloc d'action (robot à l'arrêt) ;
// resume(true) exécute le bloc suivant, resume(false) continue normalement.
class Runner : public QObject
{
    Q_OBJECT

public:
    explicit Runner(Link &link, QObject *parent = nullptr);

    void start(const Block *first, bool stepByStep = false);
    void resume(bool stepByStep);
    void stop();  // arrête le programme et le robot
    bool isRunning() const { return running_; }
    bool isPaused() const { return paused_; }

signals:
    void current(const Block *block);  // nullptr à la fin
    void sent(char cmd);               // commande envoyée (ou simulée)
    void paused(const Block *next);    // pas à pas : en attente avant ce bloc
    void finished();

private:
    struct Frame {
        const Block *next;  // prochain bloc de cette séquence
        const Block *loop;  // boucle qui répète la séquence (nullptr = script)
        int remaining;      // tours restants (répéter N fois)
        quint64 waits;      // waits_ au début du tour
    };

    void send(char cmd);
    void step();
    void onTimeout();
    void wait(double seconds);
    void finish();

    Link &link_;
    QTimer timer_;
    QList<Frame> frames_;
    quint64 waits_ = 0;  // nombre d'attentes depuis le début
    bool moving_ = false;  // commande envoyée, stop à envoyer à la fin de l'attente
    bool running_ = false;
    bool stepByStep_ = false;
    bool paused_ = false;
    bool acted_ = false;  // un bloc d'action exécuté depuis la dernière reprise
};

#pragma once

#include "blocks.h"
#include "calibration.h"
#include "config.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>

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

    // resetSpeed : remettre la vitesse par défaut (pas pour un script de touche, qui
    // garde la vitesse réglée par un autre script de touche)
    void start(const Block *first, bool stepByStep = false, bool resetSpeed = true);
    void resume(bool stepByStep);
    void stop();  // arrête le programme et le robot
    bool isRunning() const { return running_; }
    bool isPaused() const { return paused_; }

    // Variables : gardent leur valeur d'une exécution à l'autre (comme dans Scratch)
    double variable(const QString &name) const { return vars_.value(name); }
    void resetVariables();
    double evaluate(const Block *reporter);  // valeur d'un reporter seul (clic dessus)

    // Blocs en cm / degrés : durée calculée avec ce calibrage, au niveau de vitesse en cours
    void setCalibration(const Calibration &c) { calibration_ = c; }

    // Mes blocs : script « définir nom » à exécuter pour un appel (nullptr si inconnu)
    void setProcedures(std::function<const Block *(const QString &)> lookup) { procedures_ = std::move(lookup); }

signals:
    void current(const Block *block);  // nullptr à la fin
    void sent(char cmd);               // commande envoyée (ou simulée)
    void paused(const Block *next);    // pas à pas : en attente avant ce bloc
    void variableChanged(const QString &name, double value);
    void error(const QString &message);  // programme arrêté (ex. appels sans fin)
    void finished();

private:
    struct Frame {
        const Block *next;  // prochain bloc de cette séquence
        const Block *loop;  // boucle qui répète la séquence (nullptr = script)
        int remaining;      // tours restants (répéter N fois)
        quint64 waits;      // waits_ au début du tour
    };

    double input(const Block *b, int i);  // valeur d'une entrée (nombre ou reporter)
    bool condition(const Block *b, int i);  // entrée de condition : vide = faux
    double input(const Block *b, int i, Param p) { return clampValue(p, input(b, i)); }
    void setVariable(const QString &name, double value);
    void send(char cmd);
    void stopRolling();
    void step();
    void onTimeout();
    void wait(double seconds);
    void finish();

    Link &link_;
    QTimer timer_;
    QList<Frame> frames_;
    QHash<QString, double> vars_;
    std::function<const Block *(const QString &)> procedures_;
    Calibration calibration_;
    int speedLevel_ = SpeedLevelDefault;  // dernier chiffre envoyé (comme le firmware)
    quint64 waits_ = 0;  // nombre d'attentes depuis le début
    bool moving_ = false;  // commande envoyée, stop à envoyer à la fin de l'attente
    bool rolling_ = false; // « démarrer … » envoyé : le robot bouge jusqu'au prochain stop
    bool running_ = false;
    bool stepByStep_ = false;
    bool paused_ = false;
    bool acted_ = false;  // un bloc d'action exécuté depuis la dernière reprise
};

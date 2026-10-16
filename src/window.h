#pragma once

#include "link.h"
#include "runner.h"

#include <QWidget>

class Canvas;
class Palette;
class Pairing;
class QComboBox;
class QLabel;
class QPushButton;

class Window : public QWidget
{
    Q_OBJECT

public:
    explicit Window(QWidget *parent = nullptr);

    // Accès pour les tests hors écran (pas de robot)
    Link &link() { return link_; }
    Canvas &canvas() { return *canvas_; }
    Runner &runner() { return runner_; }
    void setConnected(bool on);
    void run(const Block *first);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    // Connexion (comme la télécommande)
    void refreshDevices();
    QString selectedAddress() const;
    bool autoPairingSelected() const;
    void toggleConnection();
    void connectTo(const QString &address);
    void startPairing();
    void onConnected(const QString &address);
    void onFailed(const QString &error);
    void onLost();

    // Programme
    void onFinished();
    void newProgram();
    void openProgram();
    bool saveProgram();
    void setModified(bool on);
    bool confirmDiscard();  // programme modifié : enregistrer / abandonner / annuler
    bool confirmQuit();

    Link link_;
    Runner runner_{link_};
    bool connected_ = false;
    QString connectedName_;
    Pairing *pairing_ = nullptr;
    bool modified_ = false;
    QString fileName_;

    QComboBox *deviceBox_;
    QPushButton *btButton_;
    QLabel *status_;
    QPushButton *runButton_;
    Palette *palette_;
    Canvas *canvas_;
};

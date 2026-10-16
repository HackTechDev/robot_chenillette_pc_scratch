#pragma once

#include "link.h"
#include "runner.h"

#include <QTimer>
#include <QWidget>

class Canvas;
class Palette;
class Pairing;
class Stage;
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
    void run(const Block *first, bool stepByStep = false);

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
    void runOrContinue(bool stepByStep);  // ▶ / ⏭ : démarre, ou reprend après une pause
    void setRunButtonsEnabled(bool on);
    void onPaused();
    void onFinished();
    void newProgram();
    void openExample(const QString &title, const QString &path);
    void openProgram();
    bool saveProgram();
    void setModified(bool on);
    void updateHistoryButtons();
    void autosave();
    void restoreAutosave();
    bool confirmDiscard();  // programme modifié : enregistrer / abandonner / annuler
    bool confirmQuit();

    Link link_;
    Runner runner_{link_};
    bool connected_ = false;
    QString connectedName_;
    Pairing *pairing_ = nullptr;
    bool modified_ = false;
    QString fileName_;
    QString untitled_ = QStringLiteral("sans titre");  // titre sans fichier (ex. un exemple)
    QTimer autosaveTimer_;

    QComboBox *deviceBox_;
    QPushButton *btButton_;
    QLabel *status_;
    QPushButton *runButton_;
    QPushButton *stepButton_;
    QPushButton *undoButton_;
    QPushButton *redoButton_;
    Palette *palette_;
    Canvas *canvas_;
    Stage *stage_;
};

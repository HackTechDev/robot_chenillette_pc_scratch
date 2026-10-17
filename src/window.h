#pragma once

#include "calibration.h"
#include "link.h"
#include "runner.h"

#include <QTimer>
#include <QWidget>

class CalibrationDialog;
class Canvas;
class Palette;
class Pairing;
class Stage;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QVBoxLayout;

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
    bool eventFilter(QObject *o, QEvent *e) override;  // touches des blocs « quand la touche … »

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
    void runKeyScript(const Block *first, int key);
    bool startRun(const Block *first, bool stepByStep, bool resetSpeed, const QString &status);
    void runOrContinue(bool stepByStep);  // ▶ / ⏭ : démarre, ou reprend après une pause
    void setRunButtonsEnabled(bool on);
    void onPaused();
    void onFinished();
    void openCalibration();
    void applyCalibration();
    void runTrial(Calibration::Motion motion, int level, double seconds);
    void createVariable();
    void createBlock();
    bool createProcedure(const QString &name, const QStringList &params);  // false : nom vide ou déjà pris
    void updateMonitors();
    void showValue(const Block *reporter);
    void programLoaded();
    void newProgram();
    void openExample(const QString &title, const QString &path);
    void openProgram();
    void openRecent(const QString &path);
    bool openFile(const QString &path);
    void fillMoreMenu(QMenu *menu);
    void addRecent(const QString &path);
    void loadSettings();
    void saveSettings() const;
    bool exportImage();
    void detachStage();  // scène dans une fenêtre séparée
    void attachStage();  // de retour dans la fenêtre principale
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
    bool keyRun_ = false;  // l'exécution en cours vient d'une touche
    Calibration calibration_;
    CalibrationDialog *calibrationDialog_ = nullptr;
    BlockPtr trial_;  // programme d'un essai de calibrage (gardé pendant l'exécution)
    QString connectedName_;
    Pairing *pairing_ = nullptr;
    bool modified_ = false;
    QString fileName_;
    QStringList recentFiles_;  // chemins absolus, le plus récent d'abord
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
    QVBoxLayout *stageCol_;
    QLabel *stagePlaceholder_;
    QPushButton *stageWindowButton_;
    QWidget *stageWindow_ = nullptr;
    QByteArray stageGeometry_;  // position et taille de la fenêtre de la scène
};

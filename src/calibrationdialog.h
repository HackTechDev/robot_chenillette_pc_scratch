#pragma once

#include "calibration.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;

// Assistant de calibrage : fait rouler le robot (par la fenêtre principale), on mesure,
// il calcule. Ligne droite : essais de 1 s et 3 s, distances au mètre ruban. Pivots : on
// ajuste la durée jusqu'à un tour complet exact.
class CalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    CalibrationDialog(const Calibration &calibration, QWidget *parent = nullptr);

    void setRunning(bool on);    // essai en cours : boutons Lancer désactivés
    void setConnected(bool on);  // sans robot, l'essai ne se voit que dans la scène

signals:
    void trialRequested(Calibration::Motion motion, int level, double seconds);
    void calibrationChanged(const Calibration &calibration);

private:
    Calibration::Motion motion() const;
    void updatePage();
    void updateSummary();
    void saveStraight();
    void saveTurn();
    void resetMotion();
    void store(const Calibration::Rate &r, const QString &message);

    Calibration calibration_;
    bool running_ = false;
    QComboBox *motion_;
    QSpinBox *level_;
    QStackedWidget *pages_;
    QList<QPushButton *> runButtons_;
    QDoubleSpinBox *d1_;
    QDoubleSpinBox *d3_;
    QDoubleSpinBox *turnTime_;
    QLabel *result_;
    QLabel *summary_;
    QLabel *warning_;
};

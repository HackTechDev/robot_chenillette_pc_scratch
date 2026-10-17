#pragma once

#include "calibration.h"
#include "config.h"

#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QTimer>
#include <QWidget>

// Scène : le robot vu de dessus sur un terrain de 160 × 120 cm (carreaux de 10 cm), animé
// d'après les commandes du programme (avec ou sans robot connecté). Vitesses du calibrage,
// rampe d'accélération en ligne droite comme le firmware.
class Stage : public QWidget
{
    Q_OBJECT

public:
    explicit Stage(QWidget *parent = nullptr);

    void command(char cmd);  // même protocole que le robot
    void setCalibration(const Calibration &c) { calibration_ = c; }
    void reset();            // robot au centre, trace effacée
    // Variables affichées en haut à gauche, dans l'ordre (nom, valeur)
    void setMonitors(const QList<QPair<QString, double>> &monitors);

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    void advance();  // applique le mouvement en cours depuis le dernier appel
    void move(qreal distance);  // cm
    void drive(qreal dt);       // ligne droite, avec rampe
    void tick();
    bool clawMoving() const { return motion_ == 'c' || motion_ == 'v'; }
    qreal clawZoomOpacity() const;  // loupe sur la pince : 0 = cachée
    void paintClawZoom(QPainter &p, qreal opacity) const;

    Calibration calibration_;
    QPointF pos_;           // cm
    qreal velocity_ = 0;    // cm/s, signée (rampe en ligne droite)
    qreal heading_ = 0;  // degrés, 0 = vers le haut, sens horaire
    qreal claw_ = 0;     // 0 = fermée, 1 = ouverte
    qreal head_ = 0.5;   // 0 = basse, 1 = haute
    char motion_ = Stop;
    int speedLevel_ = SpeedLevelDefault;
    QList<QPointF> trail_;
    QList<QPair<QString, double>> monitors_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool clawZoom_ = false;     // loupe affichée (pendant et juste après un mouvement de pince)
    char clawDirection_ = 0;    // dernier mouvement de pince : 'c' ou 'v'
    QElapsedTimer clawSince_;   // fin du mouvement de pince
};

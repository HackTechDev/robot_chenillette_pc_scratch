#pragma once

#include "calibration.h"
#include "config.h"

#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QTimer>
#include <QWidget>

// Scène : le robot vu de dessus sur un terrain de 4,80 × 3,60 m (carreaux de 10 cm ; la vue à
// 100 % en montre 160 × 120 cm et suit le robot), animé
// d'après les commandes du programme (avec ou sans robot connecté). Vitesses du calibrage,
// rampe d'accélération en ligne droite comme le firmware.
class Stage : public QWidget
{
    Q_OBJECT

public:
    explicit Stage(QWidget *parent = nullptr);

    void command(char cmd);  // même protocole que le robot
    void setCalibration(const Calibration &c) { calibration_ = c; }
    void setSpeech(const QString &text);  // bulle « dire » au-dessus du robot (vide : aucune)
    void reset();            // robot au centre, trace effacée
    // Variables affichées en haut à gauche, dans l'ordre (nom, valeur)
    void setMonitors(const QList<QPair<QString, double>> &monitors);

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;

protected:
    void paintEvent(QPaintEvent *e) override;
    // Vue : molette = zoom autour de la souris, glisser = déplacer, double-clic = 100 %
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    QTransform fitTransform() const;   // unités de dessin (480 × 360) → widget
    QTransform viewTransform() const;  // zoom de la vue, en unités de dessin
    void setView(qreal zoom, QPointF center);  // bornée : la vue reste sur le terrain
    bool canPan() const;
    void followRobot();
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
    QString speech_;
    qreal viewZoom_ = 1;
    QPointF viewCenter_;  // point du dessin au centre de la vue
    QPointF dragLast_;    // glisser la vue (coordonnées du widget)
    bool dragging_ = false;
    QTimer timer_;
    QElapsedTimer clock_;
    bool clawZoom_ = false;     // loupe affichée (pendant et juste après un mouvement de pince)
    char clawDirection_ = 0;    // dernier mouvement de pince : 'c' ou 'v'
    QElapsedTimer clawSince_;   // fin du mouvement de pince
};

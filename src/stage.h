#pragma once

#include "config.h"

#include <QElapsedTimer>
#include <QList>
#include <QPointF>
#include <QTimer>
#include <QWidget>

// Scène : le robot vu de dessus, animé d'après les commandes du programme (avec ou sans
// robot connecté). Vitesses approximatives : à régler sur le vrai robot (stage.cpp).
class Stage : public QWidget
{
    Q_OBJECT

public:
    explicit Stage(QWidget *parent = nullptr);

    void command(char cmd);  // même protocole que le robot
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
    void move(qreal distance);

    QPointF pos_;
    qreal heading_ = 0;  // degrés, 0 = vers le haut, sens horaire
    qreal claw_ = 0;     // 0 = fermée, 1 = ouverte
    qreal head_ = 0.5;   // 0 = basse, 1 = haute
    char motion_ = Stop;
    int speedLevel_ = SpeedLevelDefault;
    QList<QPointF> trail_;
    QList<QPair<QString, double>> monitors_;
    QTimer timer_;
    QElapsedTimer clock_;
};

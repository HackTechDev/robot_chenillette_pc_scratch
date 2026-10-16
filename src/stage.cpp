#include "stage.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

// Scène de 480 × 360 unités, comme celle de Scratch
constexpr qreal StageW = 480;
constexpr qreal StageH = 360;
constexpr qreal RobotRadius = 26;  // garde le robot entier sur la scène

// Approximations (vitesse firmware 40..400) : à la vitesse par défaut (200),
// 100 unités/s en ligne droite et 180°/s en pivot
constexpr qreal UnitsPerSpeed = 0.5;
constexpr qreal DegreesPerSpeed = 0.9;
constexpr qreal ClawTravelS = 1.0;  // pince : fermée → ouverte
constexpr qreal HeadTravelS = 1.0;  // tête : basse → haute
constexpr int FrameMs = 16;

constexpr qreal Pi = 3.14159265358979323846;

}  // namespace

Stage::Stage(QWidget *parent) : QWidget(parent)
{
    timer_.setInterval(FrameMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        advance();
        update();
    });
    reset();
}

QSize Stage::sizeHint() const
{
    return QSize(360, 270);
}

int Stage::heightForWidth(int w) const
{
    return int(w * StageH / StageW);
}

void Stage::reset()
{
    advance();
    pos_ = QPointF(StageW / 2, StageH / 2);
    heading_ = 0;
    trail_ = {pos_};
    update();
}

void Stage::command(char cmd)
{
    advance();  // termine le mouvement précédent jusqu'à maintenant
    if (cmd >= '0' && cmd <= '9') {
        speedLevel_ = cmd - '0';  // ne change pas le mouvement en cours, comme le firmware
        return;
    }
    motion_ = cmd;
    if (motion_ == Stop) {
        timer_.stop();
    } else if (!timer_.isActive()) {
        clock_.start();
        timer_.start();
    }
    update();
}

void Stage::advance()
{
    if (!timer_.isActive())
        return;
    const qreal dt = clock_.nsecsElapsed() / 1e9;  // restart() tronquerait à la ms à chaque image
    clock_.restart();
    const qreal speed = (speedLevel_ + 1) * SpeedPerLevel;
    switch (motion_) {
    case 'z': move(speed * UnitsPerSpeed * dt); break;
    case 's': move(-speed * UnitsPerSpeed * dt); break;
    case 'q': heading_ -= speed * DegreesPerSpeed * dt; break;
    case 'd': heading_ += speed * DegreesPerSpeed * dt; break;
    case 'c': claw_ = qMin(1.0, claw_ + dt / ClawTravelS); break;
    case 'v': claw_ = qMax(0.0, claw_ - dt / ClawTravelS); break;
    case 'w': head_ = qMin(1.0, head_ + dt / HeadTravelS); break;
    case 'x': head_ = qMax(0.0, head_ - dt / HeadTravelS); break;
    default: break;
    }
    heading_ = std::fmod(heading_ + 360, 360);
}

void Stage::move(qreal distance)
{
    const qreal a = heading_ * Pi / 180;
    pos_ += QPointF(std::sin(a) * distance, -std::cos(a) * distance);
    // Le robot bute sur les bords
    pos_.setX(qBound(RobotRadius, pos_.x(), StageW - RobotRadius));
    pos_.setY(qBound(RobotRadius, pos_.y(), StageH - RobotRadius));
    if (QLineF(trail_.last(), pos_).length() > 1.5)
        trail_.append(pos_);
}

void Stage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x11, 0x11, 0x11));

    // Scène centrée, à l'échelle
    const qreal scale = qMin(width() / StageW, height() / StageH);
    p.translate((width() - StageW * scale) / 2, (height() - StageH * scale) / 2);
    p.scale(scale, scale);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xF4, 0xF4, 0xF4));
    p.drawRoundedRect(QRectF(0, 0, StageW, StageH), 6, 6);
    p.setPen(QPen(QColor(0xDD, 0xDD, 0xDD), 1));
    for (qreal x = 40; x < StageW; x += 40)
        p.drawLine(QPointF(x, 0), QPointF(x, StageH));
    for (qreal y = 40; y < StageH; y += 40)
        p.drawLine(QPointF(0, y), QPointF(StageW, y));

    // Trace du parcours
    p.setPen(QPen(QColor(0x4C, 0x97, 0xFF), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(trail_.constData(), int(trail_.size()));

    // Robot, avant vers le haut
    p.save();
    p.translate(pos_);
    p.rotate(heading_);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x55, 0x55, 0x55));
    p.drawRoundedRect(QRectF(-24, -20, 9, 42), 3, 3);  // chenilles
    p.drawRoundedRect(QRectF(15, -20, 9, 42), 3, 3);
    p.setPen(QPen(QColor(0xFF, 0xD7, 0x00), 2));
    p.setBrush(QColor(0x2A, 0x2A, 0x2A));
    p.drawRoundedRect(QRectF(-15, -18, 30, 38), 5, 5);  // châssis

    // Pince : deux mâchoires à l'avant, écartées selon l'ouverture
    p.setPen(QPen(QColor(0x99, 0x66, 0xFF), 4, Qt::SolidLine, Qt::RoundCap));
    const qreal open = 8 + claw_ * 30;  // degrés
    for (int side : {-1, 1}) {
        p.save();
        p.translate(side * 7, -18);
        p.rotate(side * open);
        p.drawLine(QPointF(0, 0), QPointF(0, -14));
        p.restore();
    }

    // Tête : l'œil avance quand elle se lève
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xFF, 0xD7, 0x00));
    p.drawEllipse(QPointF(0, -2), 7, 7);
    p.setBrush(QColor(0x11, 0x11, 0x11));
    p.drawEllipse(QPointF(0, -2 - head_ * 5), 2.5, 2.5);
    p.restore();

    // Jauge de la tête
    const QRectF gauge(StageW - 18, StageH - 70, 8, 56);
    p.setPen(QPen(QColor(0xBB, 0xBB, 0xBB), 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(gauge, 4, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x99, 0x66, 0xFF));
    p.drawRoundedRect(QRectF(gauge.left(), gauge.bottom() - head_ * gauge.height(), gauge.width(),
                             head_ * gauge.height()), 4, 4);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor(0x88, 0x88, 0x88));
    p.drawText(QRectF(StageW - 60, StageH - 14, 56, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("tête"));
}

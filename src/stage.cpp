#include "stage.h"

#include "blocks.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

namespace {

// Dessin sur 480 × 360 unités, comme la scène de Scratch ; terrain de 160 × 120 cm
constexpr qreal StageW = 480;
constexpr qreal StageH = 360;
constexpr qreal UnitsPerCm = 3;
constexpr qreal FieldW = StageW / UnitsPerCm;
constexpr qreal FieldH = StageH / UnitsPerCm;
constexpr qreal GridCm = 10;
constexpr qreal RobotRadius = 7;     // cm : garde le robot entier sur le terrain
constexpr qreal RobotDrawScale = 0.25;  // dessin du robot (±24) → ~12 cm de large
constexpr qreal MaxViewZoom = 6;
constexpr qreal ViewZoomStep = 1.25;
constexpr qreal ClawTravelS = 1.0;  // pince : fermée → ouverte
constexpr qreal HeadTravelS = 1.0;  // tête : basse → haute
constexpr int FrameMs = 16;
constexpr int ClawZoomHoldMs = 1000;  // loupe sur la pince gardée après le mouvement…
constexpr int ClawZoomFadeMs = 400;   // … puis effacée en fondu

constexpr qreal Pi = 3.14159265358979323846;

const QColor ClawColor(0x99, 0x66, 0xFF);

// Mâchoires de la pince, pivots en (±spread, 0), pointées vers le haut (avant du robot) ;
// chaque mâchoire se termine par un crochet tourné vers l'intérieur
void drawJaws(QPainter &p, qreal claw, qreal spread, qreal length, qreal width)
{
    const qreal open = 5 + claw * 35;  // degrés
    p.setPen(QPen(ClawColor, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    for (int side : {-1, 1}) {
        p.save();
        p.translate(side * spread, 0);
        p.rotate(side * open);
        QPainterPath jaw;
        jaw.moveTo(0, 0);
        jaw.lineTo(0, -length);
        jaw.lineTo(-side * length * 0.35, -length * 1.2);
        p.drawPath(jaw);
        p.restore();
    }
}

}  // namespace

Stage::Stage(QWidget *parent) : QWidget(parent)
{
    viewCenter_ = QPointF(StageW / 2, StageH / 2);
    timer_.setInterval(FrameMs);
    connect(&timer_, &QTimer::timeout, this, &Stage::tick);
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
    pos_ = QPointF(FieldW / 2, FieldH / 2);
    heading_ = 0;
    trail_ = {pos_};
    update();
}

// --- Vue zoomable ---

QTransform Stage::fitTransform() const
{
    // Scène centrée, à l'échelle du widget
    const qreal scale = qMin(width() / StageW, height() / StageH);
    QTransform t;
    t.translate((width() - StageW * scale) / 2, (height() - StageH * scale) / 2);
    t.scale(scale, scale);
    return t;
}

QTransform Stage::viewTransform() const
{
    QTransform t;
    t.translate(StageW / 2, StageH / 2);
    t.scale(viewZoom_, viewZoom_);
    t.translate(-viewCenter_.x(), -viewCenter_.y());
    return t;
}

void Stage::setView(qreal zoom, QPointF center)
{
    viewZoom_ = qBound(1.0, zoom, MaxViewZoom);
    const qreal hw = StageW / (2 * viewZoom_), hh = StageH / (2 * viewZoom_);
    viewCenter_ = QPointF(qBound(hw, center.x(), StageW - hw), qBound(hh, center.y(), StageH - hh));
    setCursor(viewZoom_ > 1 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void Stage::wheelEvent(QWheelEvent *e)
{
    const int delta = e->angleDelta().y();
    if (delta == 0)
        return e->ignore();
    // Le point du dessin sous la souris reste sous la souris
    const QPointF p = fitTransform().inverted().map(e->position());
    const QPointF world = viewTransform().inverted().map(p);
    const qreal zoom = qBound(1.0, viewZoom_ * std::pow(ViewZoomStep, delta / 120.0), MaxViewZoom);
    setView(zoom, world - (p - QPointF(StageW / 2, StageH / 2)) / zoom);
    e->accept();
}

void Stage::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || viewZoom_ <= 1)
        return QWidget::mousePressEvent(e);
    dragging_ = true;
    dragLast_ = e->position();
    setCursor(Qt::ClosedHandCursor);
}

void Stage::mouseMoveEvent(QMouseEvent *e)
{
    if (!dragging_)
        return QWidget::mouseMoveEvent(e);
    const qreal fit = fitTransform().m11();
    const QPointF delta = (e->position() - dragLast_) / (fit * viewZoom_);
    dragLast_ = e->position();
    setView(viewZoom_, viewCenter_ - delta);
}

void Stage::mouseReleaseEvent(QMouseEvent *e)
{
    if (!dragging_)
        return QWidget::mouseReleaseEvent(e);
    dragging_ = false;
    setCursor(viewZoom_ > 1 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void Stage::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        setView(1, QPointF(StageW / 2, StageH / 2));
}

void Stage::setSpeech(const QString &text)
{
    speech_ = text;
    update();
}

void Stage::setMonitors(const QList<QPair<QString, double>> &monitors)
{
    monitors_ = monitors;
    update();
}

void Stage::command(char cmd)
{
    advance();  // termine le mouvement précédent jusqu'à maintenant
    if (cmd >= '0' && cmd <= '9') {
        speedLevel_ = cmd - '0';  // ne change pas le mouvement en cours, comme le firmware
        return;
    }
    if (clawMoving() && cmd != motion_)
        clawSince_.start();  // fin du mouvement de pince : la loupe reste un moment
    if (cmd != 'z' && cmd != 's')
        velocity_ = 0;  // arrêt et pivot immédiats ; z ↔ s repasse par la rampe
    motion_ = cmd;
    if (clawMoving()) {
        clawZoom_ = true;
        clawDirection_ = motion_;
    }
    if (motion_ == Stop) {
        if (!clawZoom_)
            timer_.stop();  // sinon le minuteur continue pour effacer la loupe
    } else if (!timer_.isActive()) {
        clock_.start();
        timer_.start();
    }
    update();
}

void Stage::tick()
{
    advance();
    if (clawZoom_ && clawZoomOpacity() <= 0)
        clawZoom_ = false;
    if (motion_ == Stop && !clawZoom_)
        timer_.stop();
    update();
}

qreal Stage::clawZoomOpacity() const
{
    if (!clawZoom_)
        return 0;
    if (clawMoving() || !clawSince_.isValid())
        return 1;
    const qint64 t = clawSince_.elapsed() - ClawZoomHoldMs;
    return t <= 0 ? 1 : qMax(0.0, 1 - qreal(t) / ClawZoomFadeMs);
}

void Stage::paintClawZoom(QPainter &p, qreal opacity) const
{
    // Loupe en bas à gauche : la pince vue de près
    const QRectF box(8, StageH - 108, 136, 100);
    p.save();
    p.setOpacity(opacity);
    p.setPen(QPen(QColor(0xC8, 0xC8, 0xC8), 1));
    p.setBrush(Qt::white);
    p.drawRoundedRect(box, 8, 8);

    p.save();
    p.translate(box.center().x(), box.top() + 74);
    // Support de la pince (avant du châssis)
    p.setPen(QPen(QColor(0xFF, 0xD7, 0x00), 2));
    p.setBrush(QColor(0x2A, 0x2A, 0x2A));
    p.drawRoundedRect(QRectF(-34, 0, 68, 14), 4, 4);
    drawJaws(p, claw_, 14, 30, 7);
    // Pivots
    p.setPen(Qt::NoPen);
    p.setBrush(ClawColor.darker(140));
    p.drawEllipse(QPointF(-14, 0), 4, 4);
    p.drawEllipse(QPointF(14, 0), 4, 4);
    p.restore();

    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x57, 0x5E, 0x75));
    QString text;
    if (clawMoving())
        text = clawDirection_ == 'c' ? QStringLiteral("s'ouvre…") : QStringLiteral("se ferme…");
    else
        text = claw_ >= 0.99 ? QStringLiteral("ouverte") : claw_ <= 0.01 ? QStringLiteral("fermée")
                                                         : QStringLiteral("ouverte à %1 %").arg(qRound(claw_ * 100));
    p.drawText(QRectF(box.left(), box.top() + 4, box.width(), 18), Qt::AlignCenter,
               QStringLiteral("pince : %1").arg(text));
    p.restore();
}

void Stage::advance()
{
    if (!timer_.isActive())
        return;
    const qreal dt = clock_.nsecsElapsed() / 1e9;  // restart() tronquerait à la ms à chaque image
    clock_.restart();
    switch (motion_) {
    case 'z':
    case 's': drive(dt); break;
    case 'q': heading_ -= calibration_.rate(Calibration::Left, speedLevel_).perS * dt; break;
    case 'd': heading_ += calibration_.rate(Calibration::Right, speedLevel_).perS * dt; break;
    case 'c': claw_ = qMin(1.0, claw_ + dt / ClawTravelS); break;
    case 'v': claw_ = qMax(0.0, claw_ - dt / ClawTravelS); break;
    case 'w': head_ = qMin(1.0, head_ + dt / HeadTravelS); break;
    case 'x': head_ = qMax(0.0, head_ - dt / HeadTravelS); break;
    default: break;
    }
    heading_ = std::fmod(heading_ + 360, 360);
}

void Stage::drive(qreal dt)
{
    // Vitesse visée et accélération de la rampe : vitesse établie atteinte en 2 t0
    const bool forward = motion_ == 'z';
    const Calibration::Rate r = calibration_.rate(forward ? Calibration::Forward : Calibration::Backward, speedLevel_);
    const qreal target = forward ? r.perS : -r.perS;
    qreal distance = 0;
    if (r.t0 <= 0) {
        velocity_ = target;
        distance = target * dt;
    } else {
        const qreal dv = target - velocity_;
        const qreal accel = (dv > 0 ? 1 : -1) * r.perS / (2 * r.t0);
        const qreal ramp = dv / accel;  // temps pour atteindre la vitesse visée (≥ 0)
        const qreal t = qMin(dt, ramp);
        distance = velocity_ * t + accel * t * t / 2;
        velocity_ += accel * t;
        if (dt > ramp) {
            velocity_ = target;
            distance += target * (dt - ramp);
        }
    }
    move(distance);
}

void Stage::move(qreal distance)
{
    const qreal a = heading_ * Pi / 180;
    pos_ += QPointF(std::sin(a) * distance, -std::cos(a) * distance);
    // Le robot bute sur les bords
    pos_.setX(qBound(RobotRadius, pos_.x(), FieldW - RobotRadius));
    pos_.setY(qBound(RobotRadius, pos_.y(), FieldH - RobotRadius));
    if (QLineF(trail_.last(), pos_).length() > 0.5)
        trail_.append(pos_);
}

void Stage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x11, 0x11, 0x11));

    // Scène centrée, à l'échelle
    p.setTransform(fitTransform());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xF4, 0xF4, 0xF4));
    p.drawRoundedRect(QRectF(0, 0, StageW, StageH), 6, 6);
    // Terrain en cm : carreaux de 10 cm, avec le zoom de la vue
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, StageW, StageH), 6, 6);
    p.setClipPath(clip);
    p.setTransform(viewTransform(), true);
    p.scale(UnitsPerCm, UnitsPerCm);
    QPen grid(QColor(0xDD, 0xDD, 0xDD), 1);
    grid.setCosmetic(true);
    p.setPen(grid);
    for (qreal x = GridCm; x < FieldW; x += GridCm)
        p.drawLine(QPointF(x, 0), QPointF(x, FieldH));
    for (qreal y = GridCm; y < FieldH; y += GridCm)
        p.drawLine(QPointF(0, y), QPointF(FieldW, y));

    // Trace du parcours
    p.setPen(QPen(QColor(0x4C, 0x97, 0xFF), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(trail_.constData(), int(trail_.size()));

    // Robot, avant vers le haut
    p.save();
    p.translate(pos_);
    p.rotate(heading_);
    p.scale(RobotDrawScale, RobotDrawScale);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x55, 0x55, 0x55));
    p.drawRoundedRect(QRectF(-24, -20, 9, 42), 3, 3);  // chenilles
    p.drawRoundedRect(QRectF(15, -20, 9, 42), 3, 3);
    p.setPen(QPen(QColor(0xFF, 0xD7, 0x00), 2));
    p.setBrush(QColor(0x2A, 0x2A, 0x2A));
    p.drawRoundedRect(QRectF(-15, -18, 30, 38), 5, 5);  // châssis

    // Pince : deux mâchoires à l'avant, écartées selon l'ouverture
    p.save();
    p.translate(0, -18);
    drawJaws(p, claw_, 6, 13, 3.5);
    p.restore();

    // Tête : l'œil avance quand elle se lève
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xFF, 0xD7, 0x00));
    p.drawEllipse(QPointF(0, -2), 7, 7);
    p.setBrush(QColor(0x11, 0x11, 0x11));
    p.drawEllipse(QPointF(0, -2 - head_ * 5), 2.5, 2.5);
    p.restore();
    p.restore();  // fin du terrain en cm

    if (viewZoom_ > 1) {
        QFont zf = font();
        zf.setPixelSize(11);
        p.setFont(zf);
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(QRectF(0, StageH - 16, StageW, 14), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("zoom ×%1 · double-clic : 100 %").arg(QString::number(viewZoom_, 'f', 1).replace('.', ',')));
    }

    // Variables, comme les moniteurs de Scratch
    QFont mf = font();
    mf.setPixelSize(12);
    mf.setBold(true);
    p.setFont(mf);
    const QFontMetricsF fm(mf);
    qreal my = 6;
    for (const auto &[name, value] : monitors_) {
        const QString v = formatNumber(value);
        const qreal nameW = fm.horizontalAdvance(name), valueW = qMax(24.0, fm.horizontalAdvance(v) + 12);
        const QRectF box(6, my, nameW + valueW + 20, 22);
        p.setPen(QPen(QColor(0xC8, 0xC8, 0xC8), 1));
        p.setBrush(QColor(0xE9, 0xEE, 0xF2));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(QColor(0x57, 0x5E, 0x75));
        p.drawText(QRectF(box.left() + 6, my, nameW + 1, 22), Qt::AlignVCenter, name);
        const QRectF pill(box.right() - valueW - 4, my + 3, valueW, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(variablesColor()));
        p.drawRoundedRect(pill, 8, 8);
        p.setPen(Qt::white);
        p.drawText(pill, Qt::AlignCenter, v);
        my += 26;
    }

    if (const qreal opacity = clawZoomOpacity(); opacity > 0)
        paintClawZoom(p, opacity);

    if (!speech_.isEmpty()) {
        // Bulle au-dessus à droite du robot, gardée dans la scène
        QFont bf = font();
        bf.setPixelSize(14);
        p.setFont(bf);
        const QFontMetricsF bfm(bf);
        const QRectF text = bfm.boundingRect(QRectF(0, 0, 170, 1000), Qt::TextWordWrap, speech_);
        QRectF bubble(0, 0, text.width() + 20, text.height() + 14);
        const QPointF robot = viewTransform().map(pos_ * UnitsPerCm);  // bulle à taille normale, au-dessus du robot zoomé
        bubble.moveBottomLeft(robot + QPointF(16, -30));
        bubble.moveLeft(qBound(4.0, bubble.left(), StageW - bubble.width() - 4));
        bubble.moveTop(qBound(4.0, bubble.top(), StageH - bubble.height() - 4));
        QPainterPath path;
        path.addRoundedRect(bubble, 10, 10);
        QPainterPath tail;  // pointe vers le robot
        const QPointF base(qBound(bubble.left() + 14, robot.x() + 6, bubble.right() - 14), bubble.bottom());
        tail.moveTo(base + QPointF(-8, -1));
        tail.lineTo(robot + QPointF(4, -16));
        tail.lineTo(base + QPointF(8, -1));
        tail.closeSubpath();
        p.setPen(QPen(QColor(0xB0, 0xB0, 0xB0), 1.5));
        p.setBrush(Qt::white);
        p.drawPath(path.united(tail));
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawText(bubble.adjusted(10, 7, -10, -7), Qt::TextWordWrap | Qt::AlignCenter, speech_);
    }

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
    p.drawText(QRectF(StageW - 120, 2, 116, 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("carreaux de 10 cm"));
}

#include "blockview.h"

#include <QDrag>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QWidget>

namespace blockview {

namespace {

constexpr qreal Radius = 4;
constexpr qreal Notch = 5;    // profondeur de l'encoche / de la languette
constexpr qreal NotchX = 12;  // début de l'encoche depuis le bord gauche
constexpr qreal Pad = 10;
constexpr qreal Gap = 6;
constexpr qreal PillH = 24;
constexpr qreal PillMinW = 34;

// Bord supérieur parcouru vers la droite, avec l'encoche creuse où se loge le bloc précédent
void edgeRight(QPainterPath &p, qreal x0, qreal y, qreal x1, bool notch)
{
    if (notch) {
        p.lineTo(x0 + NotchX, y);
        p.lineTo(x0 + NotchX + 4, y + Notch);
        p.lineTo(x0 + NotchX + 16, y + Notch);
        p.lineTo(x0 + NotchX + 20, y);
    }
    p.lineTo(x1, y);
}

// Bord inférieur parcouru vers la gauche, avec la languette qui s'emboîte dans le bloc suivant
void edgeLeft(QPainterPath &p, qreal x0, qreal y, qreal x1, bool tab)
{
    if (tab) {
        p.lineTo(x0 + NotchX + 20, y);
        p.lineTo(x0 + NotchX + 16, y + Notch);
        p.lineTo(x0 + NotchX + 4, y + Notch);
        p.lineTo(x0 + NotchX, y);
    }
    p.lineTo(x1, y);
}

QPainterPath stackPath(qreal w, qreal h)
{
    QPainterPath p;
    p.moveTo(0, Radius);
    p.quadTo(0, 0, Radius, 0);
    edgeRight(p, 0, 0, w - Radius, true);
    p.quadTo(w, 0, w, Radius);
    p.lineTo(w, h - Radius);
    p.quadTo(w, h, w - Radius, h);
    edgeLeft(p, 0, h, Radius, true);
    p.quadTo(0, h, 0, h - Radius);
    p.closeSubpath();
    return p;
}

QPainterPath hatPath(qreal w, qreal h)
{
    QPainterPath p;
    p.moveTo(0, Dome);
    p.cubicTo(25, -Dome / 3, 75, -Dome / 3, 100, Dome);  // sommet à y = 0
    p.lineTo(w - Radius, Dome);
    p.quadTo(w, Dome, w, Dome + Radius);
    p.lineTo(w, h - Radius);
    p.quadTo(w, h, w - Radius, h);
    edgeLeft(p, 0, h, Radius, true);
    p.quadTo(0, h, 0, h - Radius);
    p.closeSubpath();
    return p;
}

QPainterPath loopPath(qreal w, qreal inner, bool cap)
{
    const qreal innerBottom = RowH + inner;
    const qreal h = innerBottom + BottomH;
    QPainterPath p;
    p.moveTo(0, Radius);
    p.quadTo(0, 0, Radius, 0);
    edgeRight(p, 0, 0, w - Radius, true);
    p.quadTo(w, 0, w, Radius);
    p.lineTo(w, RowH - Radius);
    p.quadTo(w, RowH, w - Radius, RowH);
    edgeLeft(p, ArmW, RowH, ArmW + Radius, true);  // languette vers le premier bloc du corps
    p.quadTo(ArmW, RowH, ArmW, RowH + Radius);
    p.lineTo(ArmW, innerBottom - Radius);
    p.quadTo(ArmW, innerBottom, ArmW + Radius, innerBottom);
    edgeRight(p, ArmW, innerBottom, w - Radius, true);  // encoche du dernier bloc du corps
    p.quadTo(w, innerBottom, w, innerBottom + Radius);
    p.lineTo(w, h - Radius);
    p.quadTo(w, h, w - Radius, h);
    edgeLeft(p, 0, h, Radius, !cap);
    p.quadTo(0, h, 0, h - Radius);
    p.closeSubpath();
    return p;
}

qreal minWidth(Shape s)
{
    switch (s) {
    case Shape::Hat: return 120;
    case Shape::C:
    case Shape::CapC: return 100;
    case Shape::Stack: break;
    }
    return 60;
}

}  // namespace

QFont font()
{
    QFont f;
    f.setPixelSize(13);
    f.setBold(true);
    return f;
}

qreal layout(BlockPtr &first, QPointF pos, QList<Item> &out)
{
    const QFontMetricsF fm(font());
    const qreal y0 = pos.y();
    for (BlockPtr *slot = &first; *slot; slot = &(*slot)->next) {
        Block *b = slot->get();
        const Spec &s = b->spec();
        const QStringList parts = QString::fromUtf8(s.text).split(QStringLiteral("%1"));
        const QString before = parts.value(0).trimmed();
        const QString after = parts.value(1).trimmed();
        const qreal rowTop = pos.y() + (s.shape == Shape::Hat ? Dome : 0);

        Item it;
        it.block = b;
        it.slot = slot;
        it.pos = pos;
        qreal x = pos.x() + Pad;
        it.before = QRectF(x, rowTop, fm.horizontalAdvance(before) + 1, RowH);
        x = it.before.right();
        if (s.param != Param::None) {
            const qreal w = qMax(PillMinW, fm.horizontalAdvance(formatValue(s.param, b->value)) + 16);
            it.value = QRectF(x + Gap, rowTop + (RowH - PillH) / 2, w, PillH);
            x = it.value.right();
        }
        if (!after.isEmpty()) {
            it.after = QRectF(x + Gap, rowTop, fm.horizontalAdvance(after) + 1, RowH);
            x = it.after.right();
        }
        it.width = qMax(minWidth(s.shape), x - pos.x() + Pad);

        const int index = out.size();
        out.append(it);
        switch (s.shape) {
        case Shape::Hat:
            out[index].height = Dome + RowH;
            out[index].path = hatPath(it.width, Dome + RowH);
            break;
        case Shape::Stack:
            out[index].height = RowH;
            out[index].path = stackPath(it.width, RowH);
            break;
        case Shape::C:
        case Shape::CapC: {
            const qreal inner = qMax(MinInner, layout(b->body, pos + QPointF(ArmW, RowH), out));
            out[index].height = RowH + inner + BottomH;
            out[index].path = loopPath(it.width, inner, s.shape == Shape::CapC);
            break;
        }
        }
        out[index].path.translate(pos);
        pos.ry() += out[index].height;
    }
    return pos.y() - y0;
}

QRectF bounds(const QList<Item> &items)
{
    QRectF r;
    for (const Item &it : items)
        r |= it.path.boundingRect();
    return r;
}

void paint(QPainter &p, const Item &item, bool highlight)
{
    const Spec &s = item.block->spec();
    const QColor color(s.color);
    const QColor border = color.darker(130);
    if (highlight) {
        // Halo du bloc en cours d'exécution
        p.setPen(QPen(QColor(255, 241, 118), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(item.path);
    }
    p.setPen(QPen(border, 1));
    p.setBrush(color);
    p.drawPath(item.path);

    const QStringList parts = QString::fromUtf8(s.text).split(QStringLiteral("%1"));
    p.setFont(font());
    p.setPen(Qt::white);
    p.drawText(item.before, Qt::AlignLeft | Qt::AlignVCenter, parts.value(0).trimmed());
    if (!item.after.isNull())
        p.drawText(item.after, Qt::AlignLeft | Qt::AlignVCenter, parts.value(1).trimmed());
    if (!item.value.isNull()) {
        p.setPen(QPen(border, 1));
        p.setBrush(Qt::white);
        p.drawRoundedRect(item.value, PillH / 2, PillH / 2);
        p.setPen(QColor(0x57, 0x5E, 0x75));
        p.drawText(item.value, Qt::AlignCenter, formatValue(s.param, item.block->value));
    }
}

Qt::DropAction drag(QWidget *source, BlockPtr &stack, const QPointF &hot, qreal scale)
{
    // Image de la pile
    QList<Item> items;
    layout(stack, QPointF(), items);
    const QRectF r = bounds(items).adjusted(-4, -4, 4, 4);
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap pixmap((r.size() * scale * dpr).toSize());
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    {
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);
        p.translate(-r.topLeft());
        for (const Item &it : items)
            paint(p, it);
    }

    const QJsonObject o{
        {QStringLiteral("blocks"), blocksToJson(stack.get())},
        {QStringLiteral("hotX"), hot.x()},
        {QStringLiteral("hotY"), hot.y()},
    };
    auto *mime = new QMimeData;
    mime->setData(MimeType, QJsonDocument(o).toJson(QJsonDocument::Compact));
    auto *d = new QDrag(source);
    d->setMimeData(mime);
    d->setPixmap(pixmap);
    d->setHotSpot(((hot - r.topLeft()) * scale).toPoint());
    return d->exec(Qt::MoveAction);
}

BlockPtr fromMime(const QMimeData *mime, QPointF *hot)
{
    if (!mime->hasFormat(MimeType))
        return nullptr;
    const QJsonObject o = QJsonDocument::fromJson(mime->data(MimeType)).object();
    *hot = QPointF(o[QStringLiteral("hotX")].toDouble(), o[QStringLiteral("hotY")].toDouble());
    return blocksFromJson(o[QStringLiteral("blocks")].toArray());
}

}  // namespace blockview

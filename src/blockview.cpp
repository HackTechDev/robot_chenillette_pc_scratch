#include "blockview.h"

#include <QDrag>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
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

QPainterPath loopPath(qreal w, qreal rowH, qreal inner, bool cap)
{
    const qreal innerBottom = rowH + inner;
    const qreal h = innerBottom + BottomH;
    QPainterPath p;
    p.moveTo(0, Radius);
    p.quadTo(0, 0, Radius, 0);
    edgeRight(p, 0, 0, w - Radius, true);
    p.quadTo(w, 0, w, Radius);
    p.lineTo(w, rowH - Radius);
    p.quadTo(w, rowH, w - Radius, rowH);
    edgeLeft(p, ArmW, rowH, ArmW + Radius, true);  // languette vers le premier bloc du corps
    p.quadTo(ArmW, rowH, ArmW, rowH + Radius);
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

QPainterPath reporterPath(qreal w, qreal h)
{
    QPainterPath p;
    p.addRoundedRect(QRectF(0, 0, w, h), h / 2, h / 2);
    return p;
}

qreal minWidth(Shape s)
{
    switch (s) {
    case Shape::Hat: return 120;
    case Shape::C:
    case Shape::CapC: return 100;
    case Shape::Reporter: return 40;
    case Shape::Stack: break;
    }
    return 60;
}

// Morceaux du texte d'un bloc : texte, ou %1 / %2 / %v / %n
QStringList tokens(const Spec &s)
{
    static const QRegularExpression re(QStringLiteral("(%[12vn])"));
    QStringList out;
    const QString text = QString::fromUtf8(s.text);
    qsizetype last = 0;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString before = text.mid(last, m.capturedStart() - last).trimmed();
        if (!before.isEmpty())
            out.append(before);
        out.append(m.captured());
        last = m.capturedEnd();
    }
    const QString rest = text.mid(last).trimmed();
    if (!rest.isEmpty())
        out.append(rest);
    return out;
}

QString varLabel(const Block *b)
{
    return (b->name.isEmpty() ? QStringLiteral("?") : b->name) + QStringLiteral(" ▾");
}

QSizeF measure(const Block *b, const QFontMetricsF &fm);

// Hauteur de la ligne : de quoi contenir les reporters des entrées
qreal rowHeight(const Block *b, const QFontMetricsF &fm)
{
    const bool reporter = b->spec().shape == Shape::Reporter;
    qreal h = reporter ? ReporterH : RowH;
    for (const Input &in : b->inputs) {
        if (in.reporter)
            h = qMax(h, measure(in.reporter.get(), fm).height() + (reporter ? 6 : 10));
    }
    return h;
}

// Place les morceaux de texte à partir de left, sur la ligne [rowTop, rowTop + rowH] ;
// renvoie la largeur du bloc. it et childPos peuvent être nuls (mesure seule).
qreal placeTokens(const Block *b, const QFontMetricsF &fm, qreal left, qreal rowTop, qreal rowH, Item *it,
                  std::array<QPointF, 2> *childPos)
{
    const Spec &s = b->spec();
    const qreal pad = s.shape == Shape::Reporter ? 8 : Pad;
    qreal x = left + pad;
    bool first = true;
    for (const QString &t : tokens(s)) {
        if (!first)
            x += Gap;
        first = false;
        if (t == QLatin1String("%1") || t == QLatin1String("%2")) {
            const int i = t == QLatin1String("%1") ? 0 : 1;
            if (const Block *r = b->inputs[i].reporter.get()) {
                const QSizeF size = measure(r, fm);
                if (childPos)
                    (*childPos)[i] = QPointF(x, rowTop + (rowH - size.height()) / 2);
                x += size.width();
            } else {
                const qreal w = qMax(PillMinW, fm.horizontalAdvance(formatValue(s.param, b->inputs[i].value)) + 16);
                if (it)
                    it->value[i] = QRectF(x, rowTop + (rowH - PillH) / 2, w, PillH);
                x += w;
            }
        } else if (t == QLatin1String("%v")) {
            const qreal w = fm.horizontalAdvance(varLabel(b)) + 16;
            if (it)
                it->var = QRectF(x, rowTop + (rowH - PillH) / 2, w, PillH);
            x += w;
        } else {
            const QString text = t == QLatin1String("%n") ? b->name : t;
            const qreal w = fm.horizontalAdvance(text) + 1;
            if (it)
                it->texts.append({QRectF(x, rowTop, w, rowH), text});
            x += w;
        }
    }
    return qMax(minWidth(s.shape), x - left + pad);
}

QSizeF measure(const Block *b, const QFontMetricsF &fm)
{
    const qreal rowH = rowHeight(b, fm);
    return QSizeF(placeTokens(b, fm, 0, 0, rowH, nullptr, nullptr), rowH);
}

}  // namespace

int Item::valueAt(const QPointF &p) const
{
    for (int i = 0; i < 2; ++i) {
        if (!value[i].isNull() && value[i].contains(p))
            return i;
    }
    return -1;
}

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
        const qreal rowH = rowHeight(b, fm);
        const qreal rowTop = pos.y() + (s.shape == Shape::Hat ? Dome : 0);

        Item it;
        it.block = b;
        it.slot = slot;
        it.pos = pos;
        it.rowH = rowH;
        std::array<QPointF, 2> childPos;
        it.width = placeTokens(b, fm, pos.x(), rowTop, rowH, &it, &childPos);

        const int index = out.size();
        out.append(it);
        // Reporters des entrées, dessinés par-dessus le bloc
        for (int i = 0; i < 2; ++i) {
            if (b->inputs[i].reporter)
                layout(b->inputs[i].reporter, childPos[i], out);
        }
        switch (s.shape) {
        case Shape::Hat:
            out[index].height = Dome + rowH;
            out[index].path = hatPath(it.width, Dome + rowH);
            break;
        case Shape::Stack:
            out[index].height = rowH;
            out[index].path = stackPath(it.width, rowH);
            break;
        case Shape::Reporter:
            out[index].height = rowH;
            out[index].path = reporterPath(it.width, rowH);
            break;
        case Shape::C:
        case Shape::CapC: {
            const qreal inner = qMax(MinInner, layout(b->body, pos + QPointF(ArmW, rowH), out));
            out[index].height = rowH + inner + BottomH;
            out[index].path = loopPath(it.width, rowH, inner, s.shape == Shape::CapC);
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

void paint(QPainter &p, const Item &item, bool highlight, bool ghost)
{
    if (ghost) {
        p.setPen(QPen(QColor(0xAA, 0xAA, 0xAA), 1));
        p.setBrush(QColor(0x88, 0x88, 0x88, 150));
        p.drawPath(item.path);
        return;
    }
    const Block *b = item.block;
    const Spec &s = b->spec();
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

    p.setFont(font());
    p.setPen(Qt::white);
    for (const Item::Text &t : item.texts)
        p.drawText(t.rect, Qt::AlignLeft | Qt::AlignVCenter, t.text);
    for (int i = 0; i < 2; ++i) {
        if (item.value[i].isNull())
            continue;
        p.setPen(QPen(border, 1));
        p.setBrush(Qt::white);
        p.drawRoundedRect(item.value[i], PillH / 2, PillH / 2);
        p.setPen(QColor(0x57, 0x5E, 0x75));
        p.drawText(item.value[i], Qt::AlignCenter, formatValue(s.param, b->inputs[i].value));
    }
    if (!item.var.isNull()) {
        // Liste des variables : pastille foncée, comme dans Scratch
        p.setPen(QPen(border, 1));
        p.setBrush(color.darker(115));
        p.drawRoundedRect(item.var, 4, 4);
        p.setPen(Qt::white);
        p.drawText(item.var, Qt::AlignCenter, varLabel(b));
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

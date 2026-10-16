#include "palette.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr qreal Margin = 12;
constexpr qreal HeadingH = 28;
constexpr qreal Spacing = 10;
constexpr qreal SectionGap = 12;

}  // namespace

Palette::Palette(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);

    int count = 0;
    for (const Category &c : categories())
        count += c.kinds.size();
    templates_.reserve(count);  // les Item gardent l'adresse de leur pointeur

    qreal y = Margin;
    for (const Category &c : categories()) {
        headings_.append({c.name, QColor(c.color), y});
        y += HeadingH;
        for (Kind k : c.kinds) {
            templates_.push_back(std::make_unique<Block>(k));
            y += blockview::layout(templates_.back(), QPointF(Margin, y), items_) + Spacing;
        }
        y += SectionGap;
    }
    const QRectF r = blockview::bounds(items_);
    setMinimumSize(int(r.right() + Margin), int(y));
}

void Palette::setLocked(bool on)
{
    locked_ = on;
    pressItem_ = -1;
}

void Palette::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x1E, 0x1E, 0x1E));

    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    for (const Heading &h : headings_) {
        p.setPen(Qt::NoPen);
        p.setBrush(h.color);
        p.drawEllipse(QRectF(Margin, h.y + 6, 12, 12));
        p.setFont(f);
        p.setPen(QColor(0xBB, 0xBB, 0xBB));
        p.drawText(QRectF(Margin + 20, h.y, width(), 24), Qt::AlignLeft | Qt::AlignVCenter, h.name);
    }
    for (const blockview::Item &it : items_)
        blockview::paint(p, it);

    if (trashHover_) {
        // Pile survolant la palette : elle sera supprimée
        p.fillRect(rect(), QColor(229, 57, 53, 110));
        f.setPixelSize(16);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(visibleRegion().boundingRect(), Qt::AlignCenter, QStringLiteral("🗑\nDéposer ici\npour supprimer"));
    }
}

void Palette::mousePressEvent(QMouseEvent *e)
{
    pressItem_ = -1;
    if (e->button() != Qt::LeftButton || locked_)
        return;
    for (int i = 0; i < items_.size(); ++i) {
        if (items_[i].path.contains(e->position())) {
            pressItem_ = i;
            pressPos_ = e->position();
        }
    }
}

void Palette::mouseMoveEvent(QMouseEvent *e)
{
    if (pressItem_ < 0 || locked_ || !(e->buttons() & Qt::LeftButton)
        || (e->position() - pressPos_).manhattanLength() < QApplication::startDragDistance())
        return;
    const blockview::Item &it = items_[pressItem_];
    pressItem_ = -1;
    BlockPtr copy = std::make_unique<Block>(it.block->kind);
    blockview::drag(this, copy, pressPos_ - it.pos);
}

void Palette::dragEnterEvent(QDragEnterEvent *e)
{
    // Seules les piles venant de la zone de programme peuvent être jetées
    if (e->source() == this || !e->mimeData()->hasFormat(blockview::MimeType))
        return e->ignore();
    e->acceptProposedAction();
    trashHover_ = true;
    update();
}

void Palette::dragLeaveEvent(QDragLeaveEvent *)
{
    trashHover_ = false;
    update();
}

void Palette::dropEvent(QDropEvent *e)
{
    e->acceptProposedAction();  // la zone de programme abandonne la pile
    trashHover_ = false;
    update();
}

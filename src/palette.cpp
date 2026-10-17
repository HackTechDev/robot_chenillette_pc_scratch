#include "palette.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr qreal Margin = 12;
constexpr qreal HeadingH = 28;
constexpr qreal Spacing = 10;
constexpr qreal SectionGap = 12;
constexpr qreal ButtonH = 30;
constexpr qreal ButtonW = 170;

}  // namespace

Palette::Palette(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    rebuild();
}

void Palette::setVariables(const QStringList &names)
{
    if (names == variables_)
        return;
    variables_ = names;
    rebuild();
}

void Palette::rebuild()
{
    // Modèles : catégories fixes, puis Variables (un reporter par variable, mettre / ajouter)
    std::vector<BlockPtr> blocks;
    QList<int> sectionStarts;
    QList<Heading> headings;
    for (const Category &c : categories()) {
        sectionStarts.append(int(blocks.size()));
        headings.append({c.name, QColor(c.color), 0});
        for (Kind k : c.kinds)
            blocks.push_back(std::make_unique<Block>(k));
    }
    const int variablesStart = int(blocks.size());
    for (const QString &name : variables_) {
        blocks.push_back(std::make_unique<Block>(Kind::Variable));
        blocks.back()->name = name;
    }
    if (!variables_.isEmpty()) {
        for (Kind k : {Kind::SetVar, Kind::ChangeVar}) {
            blocks.push_back(std::make_unique<Block>(k));
            blocks.back()->name = variables_.first();
        }
    }

    templates_ = std::move(blocks);  // les Item gardent l'adresse de leur pointeur : plus de réallocation
    items_.clear();
    headings_.clear();
    pressItem_ = -1;
    qreal y = Margin;
    for (int i = 0; i < int(templates_.size()); ++i) {
        const int section = sectionStarts.indexOf(i);
        if (section >= 0) {
            if (section > 0)
                y += SectionGap;
            headings_.append(headings[section]);
            headings_.last().y = y;
            y += HeadingH;
        }
        if (i == variablesStart) {
            y += SectionGap;
            headings_.append({QStringLiteral("Variables"), QColor(variablesColor()), y});
            y += HeadingH;
            createButton_ = QRectF(Margin, y, ButtonW, ButtonH);
            y += ButtonH + Spacing;
        }
        y += blockview::layout(templates_[i], QPointF(Margin, y), items_) + Spacing;
    }
    if (variablesStart == int(templates_.size())) {
        // Aucune variable : section avec seulement le bouton
        y += SectionGap;
        headings_.append({QStringLiteral("Variables"), QColor(variablesColor()), y});
        y += HeadingH;
        createButton_ = QRectF(Margin, y, ButtonW, ButtonH);
        y += ButtonH + Spacing;
    }
    const QRectF r = blockview::bounds(items_) | createButton_;
    const int w = int(r.right() + Margin);
    const bool widthChange = w != minimumWidth();
    setMinimumSize(w, int(y + Margin));
    update();
    if (widthChange)
        emit widthChanged(w);
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

    p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
    p.setBrush(QColor(0x2A, 0x2A, 0x2A));
    p.drawRoundedRect(createButton_, 6, 6);
    p.setFont(f);
    p.setPen(QColor(0xDD, 0xDD, 0xDD));
    p.drawText(createButton_, Qt::AlignCenter, QStringLiteral("Créer une variable"));

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
    if (e->button() != Qt::LeftButton)
        return;
    if (createButton_.contains(e->position()))
        return emit createVariableRequested();
    if (locked_)
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
    BlockPtr copy = blocksFromJson(blocksToJson(it.block));  // garde le nom de la variable
    blockview::drag(this, copy, pressPos_ - it.pos);
}

void Palette::contextMenuEvent(QContextMenuEvent *e)
{
    for (const blockview::Item &it : items_) {
        if (it.block->kind != Kind::Variable || !it.path.contains(e->pos()))
            continue;
        QMenu menu(this);
        const QString name = it.block->name;
        QAction *del = menu.addAction(QStringLiteral("Supprimer la variable « %1 »").arg(name));
        if (menu.exec(e->globalPos()) == del)
            emit deleteVariableRequested(name);
        return;
    }
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

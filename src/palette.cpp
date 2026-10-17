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

void Palette::setProcedures(const QStringList &names)
{
    if (names == procedures_)
        return;
    procedures_ = names;
    rebuild();
}

void Palette::rebuild()
{
    // Sections : catégories fixes, puis Variables et Mes blocs (bouton de création + modèles)
    struct Section {
        Heading heading;
        Button button;
        std::vector<BlockPtr> blocks;
    };
    std::vector<Section> sections;
    for (const Category &c : categories()) {
        Section &s = sections.emplace_back();
        s.heading = {c.name, QColor(c.color), 0};
        for (Kind k : c.kinds)
            s.blocks.push_back(std::make_unique<Block>(k));
    }
    auto named = [](Kind k, const QString &name) {
        auto b = std::make_unique<Block>(k);
        b->name = name;
        return b;
    };
    Section &vars = sections.emplace_back();
    vars.heading = {QStringLiteral("Variables"), QColor(variablesColor()), 0};
    vars.button = {QRectF(), QStringLiteral("Créer une variable"), CreateVariable};
    for (const QString &name : variables_)
        vars.blocks.push_back(named(Kind::Variable, name));
    if (!variables_.isEmpty()) {
        vars.blocks.push_back(named(Kind::SetVar, variables_.first()));
        vars.blocks.push_back(named(Kind::ChangeVar, variables_.first()));
    }
    Section &procs = sections.emplace_back();
    procs.heading = {QStringLiteral("Mes blocs"), QColor(specOf(Kind::Call).color), 0};
    procs.button = {QRectF(), QStringLiteral("Créer un bloc"), CreateBlock};
    for (const QString &name : procedures_)
        procs.blocks.push_back(named(Kind::Call, name));

    // Les Item gardent l'adresse de leur pointeur : templates_ n'est plus modifié ensuite
    templates_.clear();
    for (Section &s : sections) {
        for (BlockPtr &b : s.blocks)
            templates_.push_back(std::move(b));
    }
    items_.clear();
    headings_.clear();
    buttons_.clear();
    pressItem_ = -1;
    qreal y = Margin;
    size_t next = 0;
    QRectF r;
    for (const Section &s : sections) {
        if (&s != &sections.front())
            y += SectionGap;
        headings_.append(s.heading);
        headings_.last().y = y;
        y += HeadingH;
        if (!s.button.text.isEmpty()) {
            buttons_.append(s.button);
            buttons_.last().rect = QRectF(Margin, y, ButtonW, ButtonH);
            r |= buttons_.last().rect;
            y += ButtonH + Spacing;
        }
        for (size_t n = 0; n < s.blocks.size(); ++n, ++next)
            y += blockview::layout(templates_[next], QPointF(Margin, y), items_) + Spacing;
    }
    r |= blockview::bounds(items_);
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

    p.setFont(f);
    for (const Button &b : buttons_) {
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.setBrush(QColor(0x2A, 0x2A, 0x2A));
        p.drawRoundedRect(b.rect, 6, 6);
        p.setPen(QColor(0xDD, 0xDD, 0xDD));
        p.drawText(b.rect, Qt::AlignCenter, b.text);
    }

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
    for (const Button &b : buttons_) {
        if (b.rect.contains(e->position()))
            return b.id == CreateVariable ? emit createVariableRequested() : emit createBlockRequested();
    }
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

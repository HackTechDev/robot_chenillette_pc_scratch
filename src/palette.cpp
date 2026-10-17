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

// Pour la recherche : minuscules, sans accents (« Pivoter » trouve « pivoter », « tete » trouve « tête »)
QString simplified(const QString &text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c.toLower());
    }
    return out;
}

// Texte affiché d'un modèle de la palette
QString label(const Block *b)
{
    QString text = QString::fromUtf8(b->spec().text);
    text.replace(QLatin1String("%n"), b->name).replace(QLatin1String("%v"), b->name).replace(QLatin1String("%t"), b->text);
    text.remove(QLatin1String("%1")).remove(QLatin1String("%2"));
    if (b->kind == Kind::Define || b->kind == Kind::Call)
        text += QLatin1Char(' ') + b->params.join(QLatin1Char(' '));
    return text;
}

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

void Palette::setProcedures(const QList<Procedure> &procedures)
{
    if (procedures == procedures_)
        return;
    procedures_ = procedures;
    rebuild();
}

void Palette::setFilter(const QString &filter)
{
    const QString f = simplified(filter.trimmed());
    if (f == filter_)
        return;
    filter_ = f;
    rebuild();
}

void Palette::toggleSection(const QString &name)
{
    if (!collapsed_.remove(name))
        collapsed_.insert(name);
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
    for (const Procedure &p : procedures_) {
        // L'appel (une entrée par paramètre), puis les paramètres à utiliser dans la définition
        procs.blocks.push_back(named(Kind::Call, p.name));
        procs.blocks.back()->params = p.params;
        for (const QString &param : p.params)
            procs.blocks.push_back(named(Kind::ParamValue, param));
    }

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

    // Largeur : celle de tous les modèles, pour qu'elle ne change pas pendant une recherche
    QRectF r(Margin, 0, ButtonW, 1);
    for (BlockPtr &b : templates_) {
        QList<blockview::Item> tmp;
        blockview::layout(b, QPointF(Margin, 0), tmp);
        r |= blockview::bounds(tmp);
    }

    qreal y = Margin;
    size_t next = 0;
    const bool searching = !filter_.isEmpty();
    bool firstSection = true;
    for (const Section &s : sections) {
        const size_t first = next;
        next += s.blocks.size();
        // Recherche : seulement les blocs trouvés, et leurs catégories ; sinon catégories repliables
        QList<size_t> shown;
        for (size_t n = first; n < next; ++n) {
            if (!searching || simplified(label(templates_[n].get())).contains(filter_))
                shown.append(n);
        }
        if (searching && shown.isEmpty())
            continue;
        const bool collapsed = !searching && collapsed_.contains(s.heading.name);
        if (!firstSection)
            y += SectionGap;
        firstSection = false;
        headings_.append(s.heading);
        headings_.last().y = y;
        headings_.last().collapsed = collapsed;
        y += HeadingH;
        if (collapsed)
            continue;
        if (!s.button.text.isEmpty() && !searching) {
            buttons_.append(s.button);
            buttons_.last().rect = QRectF(Margin, y, ButtonW, ButtonH);
            y += ButtonH + Spacing;
        }
        for (const size_t n : shown)
            y += blockview::layout(templates_[n], QPointF(Margin, y), items_) + Spacing;
    }
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
        // ▾ dépliée, ▸ repliée (clic sur le titre)
        const QString arrow = filter_.isEmpty() ? (h.collapsed ? QStringLiteral("▸ ") : QStringLiteral("▾ ")) : QString();
        p.drawText(QRectF(Margin + 20, h.y, width(), 24), Qt::AlignLeft | Qt::AlignVCenter, arrow + h.name);
    }
    if (headings_.isEmpty()) {
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(rect().adjusted(Margin, Margin, -Margin, 0), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap,
                   QStringLiteral("Aucun bloc trouvé"));
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
    if (filter_.isEmpty()) {
        for (const Heading &h : headings_) {
            if (QRectF(0, h.y, width(), HeadingH).contains(e->position()))
                return toggleSection(h.name);
        }
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

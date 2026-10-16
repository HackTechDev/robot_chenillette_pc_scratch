#include "canvas.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QRegularExpressionValidator>

#include <algorithm>
#include <cmath>
#include <utility>

using blockview::Item;

namespace {

constexpr qreal Margin = 8;
constexpr qreal Grid = 24;
constexpr qreal SnapDistance = 30;
constexpr int MaxHistory = 200;
const QPointF FirstScript(40, 40);
const QString Format = QStringLiteral("batmobile-scratch");

}  // namespace

Canvas::Canvas(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    editor_ = new QLineEdit(this);
    editor_->setAlignment(Qt::AlignCenter);
    editor_->setStyleSheet(QStringLiteral("QLineEdit { background: #fff; color: #222; border: 2px solid #4C97FF;"
                                          " border-radius: 12px; font-weight: bold; }"));
    editor_->hide();
    connect(editor_, &QLineEdit::editingFinished, this, &Canvas::commitEdit);
    clear();
}

// --- Programme ---

void Canvas::clear()
{
    commitEdit();
    scripts_.clear();
    addScript(FirstScript, std::make_unique<Block>(Kind::Start));
    relayout();
    resetHistory();
}

QJsonObject Canvas::save() const
{
    QJsonArray scripts;
    for (const auto &s : scripts_) {
        if (s->top) {
            scripts.append(QJsonObject{
                {QStringLiteral("x"), s->pos.x()},
                {QStringLiteral("y"), s->pos.y()},
                {QStringLiteral("blocks"), blocksToJson(s->top.get())},
            });
        }
    }
    return {{QStringLiteral("format"), Format}, {QStringLiteral("version"), 1}, {QStringLiteral("scripts"), scripts}};
}

bool Canvas::load(const QJsonObject &o)
{
    if (o[QStringLiteral("format")].toString() != Format)
        return false;
    commitEdit();
    restore(o);
    resetHistory();
    return true;
}

bool Canvas::isModified() const
{
    return history_[historyIndex_] != saved_;
}

// --- Annuler / rétablir : un état JSON complet par modification ---

void Canvas::undo()
{
    commitEdit();  // une saisie en cours compte comme une modification
    if (!canUndo())
        return;
    restore(history_[--historyIndex_]);
    emit changed();
    emit historyChanged();
}

void Canvas::redo()
{
    commitEdit();
    if (!canRedo())
        return;
    restore(history_[++historyIndex_]);
    emit changed();
    emit historyChanged();
}

void Canvas::edited()
{
    const QJsonObject state = save();
    if (state == history_[historyIndex_])
        return;  // ex. pile lâchée à sa place
    history_.resize(historyIndex_ + 1);  // l'historique « rétablir » est perdu
    history_.append(state);
    if (history_.size() > MaxHistory)
        history_.removeFirst();
    historyIndex_ = history_.size() - 1;
    emit changed();
    emit historyChanged();
}

void Canvas::resetHistory()
{
    history_ = {save()};
    historyIndex_ = 0;
    saved_ = history_[0];
    emit historyChanged();
}

void Canvas::restore(const QJsonObject &o)
{
    scripts_.clear();
    for (const QJsonValue &v : o[QStringLiteral("scripts")].toArray()) {
        const QJsonObject s = v.toObject();
        if (BlockPtr top = blocksFromJson(s[QStringLiteral("blocks")].toArray()))
            addScript(QPointF(s[QStringLiteral("x")].toDouble(), s[QStringLiteral("y")].toDouble()), std::move(top));
    }
    relayout();
}

const Block *Canvas::startScript() const
{
    const Script *best = nullptr;
    for (const auto &s : scripts_) {
        if (s->top && s->top->kind == Kind::Start
            && (!best || s->pos.y() < best->pos.y() || (s->pos.y() == best->pos.y() && s->pos.x() < best->pos.x())))
            best = s.get();
    }
    return best ? best->top.get() : nullptr;
}

void Canvas::setRunning(bool on)
{
    running_ = on;
    pressItem_ = -1;
    if (!on)
        current_ = nullptr;
    update();
    emit historyChanged();  // annuler / rétablir bloqués pendant l'exécution
}

void Canvas::setCurrent(const Block *block)
{
    current_ = block;
    update();
}

void Canvas::addScript(const QPointF &pos, BlockPtr top)
{
    auto s = std::make_unique<Script>();
    s->pos = QPointF(qMax(Margin, pos.x()), qMax(Margin, pos.y()));
    s->top = std::move(top);
    scripts_.push_back(std::move(s));
}

void Canvas::pruneScripts()
{
    std::erase_if(scripts_, [](const auto &s) { return !s->top; });
}

// --- Géométrie ---

void Canvas::relayout()
{
    items_.clear();
    itemScripts_.clear();
    for (const auto &s : scripts_) {
        if (!s->top)
            continue;
        blockview::layout(s->top, s->pos, items_);
        while (itemScripts_.size() < items_.size())
            itemScripts_.append(s.get());
    }
    const QRectF r = blockview::bounds(items_);
    if (!items_.isEmpty() && (r.left() < 0 || r.top() < 0)) {
        // Un script remonté au-dessus du bord : tout redescendre
        const QPointF shift(qMax(0.0, Margin - r.left()), qMax(0.0, Margin - r.top()));
        for (const auto &s : scripts_)
            s->pos += shift;
        return relayout();
    }
    setMinimumSize(int(r.right()) + 300, int(r.bottom()) + 300);  // place pour agrandir
    update();
}

int Canvas::itemAt(const QPointF &pos) const
{
    // Du dernier dessiné (au-dessus) au premier
    for (int i = items_.size() - 1; i >= 0; --i) {
        if (items_[i].path.contains(pos))
            return i;
    }
    return -1;
}

Canvas::Snap Canvas::findSnap(BlockPtr &stack, const QPointF &topLeft) const
{
    Snap best;
    qreal bestDist = SnapDistance;
    auto consider = [&](BlockPtr *slot, Script *before, const QPointF &at, const QPointF &probe) {
        const qreal d = QLineF(at, probe).length();
        if (d < bestDist) {
            bestDist = d;
            best = {slot, before, at};
        }
    };
    // Une boucle sans fin en bas de la pile : rien ne peut la suivre
    const bool cap = lastBlock(stack.get())->spec().shape == Shape::CapC;

    if (stack->spec().shape != Shape::Hat) {
        for (const Item &it : items_) {
            const Shape s = it.block->spec().shape;
            if (s != Shape::CapC && !(cap && it.block->next))
                consider(&it.block->next, nullptr, it.pos + QPointF(0, it.height), topLeft);
            if (hasBody(s) && !(cap && it.block->body))
                consider(&it.block->body, nullptr, it.pos + QPointF(blockview::ArmW, blockview::RowH), topLeft);
        }
    }
    if (!cap) {
        QList<Item> tmp;
        const qreal h = blockview::layout(stack, QPointF(), tmp);
        for (const auto &s : scripts_) {
            if (s->top && s->top->spec().shape != Shape::Hat)
                consider(nullptr, s.get(), s->pos, topLeft + QPointF(0, h));
        }
    }
    return best;
}

void Canvas::drop(BlockPtr stack, const QPointF &topLeft)
{
    const Snap snap = findSnap(stack, topLeft);
    Block *tail = lastBlock(stack.get());
    if (snap.slot) {
        tail->next = std::move(*snap.slot);
        *snap.slot = std::move(stack);
    } else if (snap.before) {
        QList<Item> tmp;
        snap.before->pos.ry() -= blockview::layout(stack, QPointF(), tmp);
        tail->next = std::move(snap.before->top);
        snap.before->top = std::move(stack);
    } else {
        addScript(topLeft, std::move(stack));
    }
}

// --- Souris ---

void Canvas::paintEvent(QPaintEvent *e)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x26, 0x26, 0x26));

    // Grille de points
    const QRect r = e->rect();
    p.setPen(QPen(QColor(0x3A, 0x3A, 0x3A), 2));
    for (qreal x = std::floor(r.left() / Grid) * Grid; x <= r.right(); x += Grid) {
        for (qreal y = std::floor(r.top() / Grid) * Grid; y <= r.bottom(); y += Grid)
            p.drawPoint(QPointF(x, y));
    }

    for (const Item &it : items_)
        blockview::paint(p, it, it.block == current_);

    if (snap_.valid()) {
        // Endroit où la pile va s'emboîter
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 220));
        p.drawRoundedRect(QRectF(snap_.at - QPointF(0, 3), QSizeF(90, 6)), 3, 3);
    }
}

void Canvas::mousePressEvent(QMouseEvent *e)
{
    commitEdit();
    pressItem_ = -1;
    if (e->button() == Qt::LeftButton) {
        pressItem_ = itemAt(e->position());
        pressPos_ = e->position();
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (pressItem_ < 0 || running_ || !(e->buttons() & Qt::LeftButton)
        || (e->position() - pressPos_).manhattanLength() < QApplication::startDragDistance())
        return;
    const int i = pressItem_;
    pressItem_ = -1;
    startDrag(i);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = pressItem_;
    pressItem_ = -1;
    if (i < 0 || e->button() != Qt::LeftButton)
        return;
    // Clic sur la valeur : modification ; ailleurs sur le script : exécution
    if (!items_[i].value.isNull() && items_[i].value.contains(e->position()))
        return editValue(i);
    if (!running_)
        emit runRequested(itemScripts_[i]->top.get());
}

void Canvas::contextMenuEvent(QContextMenuEvent *e)
{
    const int i = itemAt(e->pos());
    if (i < 0 || running_)
        return;
    commitEdit();
    QMenu menu(this);
    QAction *dup = menu.addAction(QStringLiteral("Dupliquer"));
    QAction *del = menu.addAction(QStringLiteral("Supprimer le bloc"));
    QAction *chosen = menu.exec(e->globalPos());
    if (chosen == dup)
        duplicate(i);
    else if (chosen == del)
        removeBlock(i);
}

void Canvas::startDrag(int item)
{
    commitEdit();
    // Le bloc part avec tous ceux qui le suivent, comme dans Scratch
    BlockPtr *slot = items_[item].slot;
    const QPointF hot = pressPos_ - items_[item].pos;
    BlockPtr stack = std::move(*slot);
    relayout();
    // Déposée ici, la pile est recopiée par dropEvent ; sur la palette, elle est jetée
    if (blockview::drag(this, stack, hot) == Qt::IgnoreAction)
        *slot = std::move(stack);  // lâchée ailleurs : remise en place
    pruneScripts();
    relayout();
    edited();
}

void Canvas::removeBlock(int item)
{
    // Supprime ce bloc seul (et le corps d'une boucle), les suivants remontent
    BlockPtr *slot = items_[item].slot;
    BlockPtr removed = std::move(*slot);
    *slot = std::move(removed->next);
    pruneScripts();
    relayout();
    edited();
}

void Canvas::duplicate(int item)
{
    const Item &it = items_[item];
    addScript(it.pos + QPointF(24, 24), blocksFromJson(blocksToJson(it.block)));
    relayout();
    edited();
}

// --- Glisser-déposer ---

void Canvas::dragEnterEvent(QDragEnterEvent *e)
{
    if (running_ || !(incoming_ = blockview::fromMime(e->mimeData(), &incomingHot_)))
        return e->ignore();
    e->acceptProposedAction();
}

void Canvas::dragMoveEvent(QDragMoveEvent *e)
{
    if (!incoming_)
        return e->ignore();
    snap_ = findSnap(incoming_, e->position() - incomingHot_);
    e->acceptProposedAction();
    update();
}

void Canvas::dragLeaveEvent(QDragLeaveEvent *)
{
    incoming_.reset();
    snap_ = {};
    update();
}

void Canvas::dropEvent(QDropEvent *e)
{
    snap_ = {};
    if (!incoming_)
        return e->ignore();
    drop(std::move(incoming_), e->position() - incomingHot_);
    e->acceptProposedAction();
    relayout();
    edited();
}

// --- Saisie d'une valeur ---

void Canvas::editValue(int item)
{
    const Item &it = items_[item];
    const Param param = it.block->spec().param;
    edited_ = it.block;
    delete editor_->validator();
    editor_->setValidator(new QRegularExpressionValidator(QRegularExpression(valuePattern(param)), editor_));
    editor_->setText(formatValue(param, it.block->value));
    QRect r = it.value.toAlignedRect().adjusted(-4, -2, 4, 2);
    r.setWidth(qMax(r.width(), 56));
    editor_->setGeometry(r);
    editor_->show();
    editor_->setFocus();
    editor_->selectAll();
}

void Canvas::commitEdit()
{
    if (!edited_)
        return;
    Block *b = std::exchange(edited_, nullptr);
    editor_->hide();  // déclenche editingFinished : edited_ est déjà remis à zéro
    double v = 0;
    if (parseValue(b->spec().param, editor_->text(), &v) && v != b->value) {
        b->value = v;
        edited();
    }
    relayout();
}

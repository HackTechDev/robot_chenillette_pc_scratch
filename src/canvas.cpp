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
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

using blockview::Item;

namespace {

constexpr qreal Margin = 8;
constexpr qreal Grid = 24;
constexpr qreal SnapDistance = 30;
constexpr int MaxHistory = 200;
constexpr qreal MinZoom = 0.5;
constexpr qreal MaxZoom = 2;
constexpr qreal ZoomStep = 1.25;
constexpr int WheelNotch = 120;
constexpr int WheelCommitMs = 600;
constexpr qreal ArrangeGap = 24;
const QPointF FirstScript(40, 40);
const QString Format = QStringLiteral("batmobile-scratch");

}  // namespace

Canvas::Canvas(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    editor_ = new QLineEdit(this);
    editor_->setAlignment(Qt::AlignCenter);
    editor_->hide();
    connect(editor_, &QLineEdit::editingFinished, this, &Canvas::commitEdit);
    wheelCommit_.setSingleShot(true);
    wheelCommit_.setInterval(WheelCommitMs);
    connect(&wheelCommit_, &QTimer::timeout, this, &Canvas::edited);
    clear();
}

// --- Programme ---

void Canvas::clear()
{
    commitEdit();
    scripts_.clear();
    addScript(FirstScript, std::make_unique<Block>(Kind::Start));
    setVariables({});
    relayout();
    updateProcedures();
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
    return {
        {QStringLiteral("format"), Format},
        {QStringLiteral("version"), 1},
        {QStringLiteral("variables"), QJsonArray::fromStringList(variables_)},
        {QStringLiteral("scripts"), scripts},
    };
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
    if (ghostSlot_)
        return;  // aperçu en cours : le dépôt enregistrera l'état
    const QJsonObject state = save();
    if (state == history_[historyIndex_])
        return;  // ex. pile lâchée à sa place
    history_.resize(historyIndex_ + 1);  // l'historique « rétablir » est perdu
    history_.append(state);
    if (history_.size() > MaxHistory)
        history_.removeFirst();
    historyIndex_ = history_.size() - 1;
    updateProcedures();
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
    QStringList names;
    for (const QJsonValue &v : o[QStringLiteral("variables")].toArray())
        names.append(v.toString());
    setVariables(names);
    relayout();
    updateProcedures();
}

void Canvas::updateProcedures()
{
    // Dans l'ordre des scripts, de haut en bas
    std::vector<const Script *> order;
    for (const auto &s : scripts_) {
        if (s->top && s->top->kind == Kind::Define && !s->top->name.isEmpty())
            order.push_back(s.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Script *a, const Script *b) { return a->pos.y() < b->pos.y(); });
    QStringList names;
    for (const Script *s : order) {
        if (!names.contains(s->top->name))
            names.append(s->top->name);
    }
    if (names == procedures_)
        return;
    procedures_ = names;
    emit proceduresChanged(procedures_);
}

const Block *Canvas::procedure(const QString &name) const
{
    return topScript([&name](const Block *top) { return top->kind == Kind::Define && top->name == name; });
}

bool Canvas::addProcedure(const QString &name)
{
    if (name.isEmpty() || procedure(name))
        return false;
    commitEdit();
    // Sous tous les scripts, à gauche
    const QRectF r = blockview::bounds(items_);
    auto define = std::make_unique<Block>(Kind::Define);
    define->name = name;
    const QPointF pos(FirstScript.x(), items_.isEmpty() ? FirstScript.y() : r.bottom() + 2 * ArrangeGap);
    addScript(pos, std::move(define));
    relayout();
    edited();
    if (QScrollArea *area = scrollArea())
        area->ensureVisible(int(pos.x() * zoom_), int((pos.y() + blockview::RowH) * zoom_), 50, int(100 * zoom_));
    return true;
}

void Canvas::setVariables(QStringList names)
{
    for (const auto &s : scripts_)
        collectVariables(s->top.get(), &names);
    names.removeAll(QString());
    names.removeDuplicates();
    if (names == variables_)
        return;
    variables_ = names;
    emit variablesChanged(variables_);
}

bool Canvas::addVariable(const QString &name)
{
    if (name.isEmpty() || variables_.contains(name))
        return false;
    setVariables(variables_ + QStringList{name});
    edited();
    return true;
}

bool Canvas::removeVariable(const QString &name)
{
    // Utilisée par un bloc : elle réapparaîtrait avec lui
    QStringList used;
    for (const auto &s : scripts_)
        collectVariables(s->top.get(), &used);
    if (used.contains(name) || !variables_.removeOne(name))
        return false;
    emit variablesChanged(variables_);
    edited();
    return true;
}

const Block *Canvas::startScript() const
{
    return topScript([](const Block *top) { return top->kind == Kind::Start; });
}

const Block *Canvas::keyScript(int key) const
{
    return topScript([key](const Block *top) { return top->kind == Kind::KeyHat && int(top->inputs[0].value) == key; });
}

const Block *Canvas::topScript(const std::function<bool(const Block *)> &match) const
{
    const Script *best = nullptr;
    for (const auto &s : scripts_) {
        if (s->top && match(s->top.get())
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
    setMinimumSize(int((r.right() + 300) * zoom_), int((r.bottom() + 300) * zoom_));  // place pour agrandir
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
    if (stack->spec().shape == Shape::Reporter) {
        // Reporter : dans une entrée libre, près de sa pastille
        QList<Item> tmp;
        const qreal h = blockview::layout(stack, QPointF(), tmp);
        for (const Item &it : items_) {
            if (!acceptsReporter(it.block->spec().param))
                continue;
            for (int i = 0; i < 2; ++i) {
                if (!it.value[i].isNull())
                    consider(&it.block->inputs[i].reporter, nullptr, QPointF(it.value[i].left(), it.value[i].center().y()),
                             topLeft + QPointF(0, h / 2));
            }
        }
        return best;
    }
    // Une boucle sans fin en bas de la pile : rien ne peut la suivre
    const bool cap = lastBlock(stack.get())->spec().shape == Shape::CapC;

    if (stack->spec().shape != Shape::Hat) {
        for (const Item &it : items_) {
            const Shape s = it.block->spec().shape;
            if (s == Shape::Reporter)
                continue;
            if (s != Shape::CapC && !(cap && it.block->next))
                consider(&it.block->next, nullptr, it.pos + QPointF(0, it.height), topLeft);
            if (hasBody(s) && !(cap && it.block->body))
                consider(&it.block->body, nullptr, it.pos + QPointF(blockview::ArmW, it.rowH), topLeft);
        }
    }
    if (!cap) {
        QList<Item> tmp;
        const qreal h = blockview::layout(stack, QPointF(), tmp);
        for (const auto &s : scripts_) {
            const Shape top = s->top ? s->top->spec().shape : Shape::Hat;
            if (top != Shape::Hat && top != Shape::Reporter)
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
    p.scale(zoom_, zoom_);

    // Grille de points
    const QRectF r(toModel(e->rect().topLeft()), QSizeF(e->rect().size()) / zoom_);
    p.setPen(QPen(QColor(0x3A, 0x3A, 0x3A), 2));
    for (qreal x = std::floor(r.left() / Grid) * Grid; x <= r.right(); x += Grid) {
        for (qreal y = std::floor(r.top() / Grid) * Grid; y <= r.bottom(); y += Grid)
            p.drawPoint(QPointF(x, y));
    }

    for (const Item &it : items_)
        blockview::paint(p, it, it.block == current_, ghostBlocks_.contains(it.block));
    for (const Item &it : ghostItems_)
        blockview::paint(p, it, false, true);
}

void Canvas::mousePressEvent(QMouseEvent *e)
{
    commitEdit();
    pressItem_ = -1;
    pressPos_ = toModel(e->position());
    if (e->button() == Qt::LeftButton)
        pressItem_ = itemAt(pressPos_);
    // Fond saisi (ou bouton du milieu) : déplacement de la vue
    if ((e->button() == Qt::LeftButton && pressItem_ < 0) || e->button() == Qt::MiddleButton) {
        panning_ = true;
        panLast_ = e->globalPosition();
        setCursor(Qt::ClosedHandCursor);
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (panning_) {
        // Position globale : le widget se déplace pendant le défilement
        const QPointF delta = e->globalPosition() - panLast_;
        panLast_ = e->globalPosition();
        if (QScrollArea *area = scrollArea()) {
            area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() - qRound(delta.x()));
            area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() - qRound(delta.y()));
        }
        return;
    }
    if (pressItem_ < 0 || running_ || !(e->buttons() & Qt::LeftButton)
        || ((toModel(e->position()) - pressPos_) * zoom_).manhattanLength() < QApplication::startDragDistance())
        return;
    const int i = pressItem_;
    pressItem_ = -1;
    startDrag(i);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (panning_) {
        panning_ = false;
        unsetCursor();
    }
    const int i = pressItem_;
    pressItem_ = -1;
    if (i < 0 || e->button() != Qt::LeftButton)
        return;
    // Clic sur une valeur ou une liste : modification ; ailleurs sur le script : exécution (selon la fenêtre)
    const QPointF m = toModel(e->position());
    if (const int input = items_[i].valueAt(m); input >= 0)
        return editValue(i, input);
    if (!items_[i].var.isNull() && items_[i].var.contains(m))
        return chooseVariable(i);
    if (!running_)
        emit scriptClicked(itemScripts_[i]->top.get());
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPointF m = toModel(e->position());
    const int i = itemAt(m);
    if (e->button() != Qt::LeftButton || i < 0 || running_ || items_[i].valueAt(m) >= 0
        || (!items_[i].var.isNull() && items_[i].var.contains(m)))
        return mousePressEvent(e);
    pressItem_ = -1;  // le relâchement qui suit n'est pas un second clic
    emit scriptDoubleClicked(itemScripts_[i]->top.get());
}

void Canvas::contextMenuEvent(QContextMenuEvent *e)
{
    if (running_)
        return;
    commitEdit();
    const int i = itemAt(toModel(e->pos()));
    if (i < 0) {
        // Menu du fond
        QMenu menu(this);
        QAction *arrange = menu.addAction(QStringLiteral("Ranger les scripts"));
        QAction *all = menu.addAction(QStringLiteral("Tout supprimer"));
        arrange->setEnabled(!items_.isEmpty());
        all->setEnabled(!items_.isEmpty());
        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == arrange)
            arrangeScripts();
        else if (chosen == all)
            removeAll();
        return;
    }
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
    const Qt::DropAction action = blockview::drag(this, stack, hot, zoom_);
    endDrop();  // au cas où aucun dragLeave n'a été reçu
    if (action == Qt::IgnoreAction)
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
    endDrop();
    if (running_ || !(incoming_ = blockview::fromMime(e->mimeData(), &incomingHot_)))
        return e->ignore();
    // Blocs de la pile survolante, dessinés en gris une fois insérés
    QList<Item> tmp;
    blockview::layout(incoming_, QPointF(), tmp);
    for (const Item &it : tmp)
        ghostBlocks_.insert(it.block);
    e->acceptProposedAction();
}

void Canvas::dragMoveEvent(QDragMoveEvent *e)
{
    if (!incoming_ && !ghostSlot_)
        return e->ignore();
    removeGhost();  // l'accroche se cherche dans le programme sans l'aperçu
    showGhost(findSnap(incoming_, toModel(e->position()) - incomingHot_));
    e->acceptProposedAction();
}

void Canvas::dragLeaveEvent(QDragLeaveEvent *)
{
    endDrop();
}

void Canvas::dropEvent(QDropEvent *e)
{
    removeGhost();
    if (!incoming_)
        return e->ignore();
    drop(std::move(incoming_), toModel(e->position()) - incomingHot_);
    endDrop();
    e->acceptProposedAction();
    edited();
}

void Canvas::showGhost(const Snap &snap)
{
    if (snap.slot) {
        // Insérée pour de vrai : les blocs suivants descendent
        ghostTail_ = lastBlock(incoming_.get());
        ghostTail_->next = std::move(*snap.slot);
        *snap.slot = std::move(incoming_);
        ghostSlot_ = snap.slot;
        relayout();
    } else if (snap.before) {
        QList<Item> tmp;
        const qreal h = blockview::layout(incoming_, QPointF(), tmp);
        blockview::layout(incoming_, snap.before->pos - QPointF(0, h), ghostItems_);
        update();
    }
}

void Canvas::removeGhost()
{
    if (ghostSlot_) {
        incoming_ = std::move(*ghostSlot_);
        *ghostSlot_ = std::move(ghostTail_->next);
        ghostSlot_ = nullptr;
        ghostTail_ = nullptr;
        relayout();
    }
    if (!ghostItems_.isEmpty()) {
        ghostItems_.clear();
        update();
    }
}

void Canvas::endDrop()
{
    removeGhost();
    incoming_.reset();
    ghostBlocks_.clear();
    relayout();
}

// --- Saisie d'une valeur ---

void Canvas::editValue(int item, int input)
{
    const Item &it = items_[item];
    const Param param = it.block->spec().param;
    if (param == Param::Key)
        return chooseKey(item);
    edited_ = it.block;
    editedInput_ = input;
    delete editor_->validator();
    editor_->setValidator(new QRegularExpressionValidator(QRegularExpression(valuePattern(param)), editor_));
    editor_->setText(formatValue(param, it.block->inputs[input].value));
    editor_->setStyleSheet(QStringLiteral("QLineEdit { background: #fff; color: #222; border: 2px solid #4C97FF;"
                                          " border-radius: %1px; font-weight: bold; font-size: %2px; }")
                               .arg(qRound(12 * zoom_))
                               .arg(qRound(13 * zoom_)));
    const QRectF pill = it.value[input];
    QRect r = QRectF(pill.topLeft() * zoom_, pill.size() * zoom_).toAlignedRect().adjusted(-4, -2, 4, 2);
    r.setWidth(qMax(r.width(), qRound(56 * zoom_)));
    editor_->setGeometry(r);
    editor_->show();
    editor_->setFocus();
    editor_->selectAll();
}

void Canvas::chooseKey(int item)
{
    // Liste déroulante : espace et flèches, puis lettres et chiffres en sous-menus
    Block *b = items_[item].block;
    double &value = b->inputs[0].value;
    const QPoint at = mapToGlobal((items_[item].value[0].bottomLeft() * zoom_).toPoint());
    QMenu menu(this);
    QMenu *letters = nullptr;
    QMenu *digits = nullptr;
    for (int i = 0; i < keyCount(); ++i) {
        const QString name = keyName(i);
        QMenu *target = &menu;
        if (name.size() == 1 && name[0].isLetter() && name[0].isLower())
            target = letters ? letters : (letters = menu.addMenu(QStringLiteral("lettres")));
        else if (name.size() == 1 && name[0].isDigit())
            target = digits ? digits : (digits = menu.addMenu(QStringLiteral("chiffres")));
        QAction *a = target->addAction(name);
        a->setData(i);
        a->setCheckable(true);
        a->setChecked(i == int(value));
    }
    const QAction *chosen = menu.exec(at);
    if (!chosen || chosen->data().toInt() == int(value))
        return;
    value = chosen->data().toInt();
    relayout();
    edited();
}

void Canvas::chooseVariable(int item)
{
    Block *b = items_[item].block;
    const QPoint at = mapToGlobal((items_[item].var.bottomLeft() * zoom_).toPoint());
    QMenu menu(this);
    for (const QString &name : variables_) {
        QAction *a = menu.addAction(name);
        a->setCheckable(true);
        a->setChecked(name == b->name);
    }
    if (variables_.isEmpty())
        menu.addAction(QStringLiteral("(créer une variable dans la palette)"))->setEnabled(false);
    const QAction *chosen = menu.exec(at);
    if (!chosen || !chosen->isEnabled() || chosen->text() == b->name)
        return;
    b->name = chosen->text();
    relayout();
    edited();
}

void Canvas::commitEdit()
{
    // Valeur changée à la molette : l'étape d'annulation est enregistrée maintenant
    if (wheelCommit_.isActive()) {
        wheelCommit_.stop();
        edited();
    }
    if (!edited_)
        return;
    Block *b = std::exchange(edited_, nullptr);
    editor_->hide();  // déclenche editingFinished : edited_ est déjà remis à zéro
    double v = 0;
    double &value = b->inputs[editedInput_].value;
    if (parseValue(b->spec().param, editor_->text(), &v) && v != value) {
        value = v;
        edited();
    }
    relayout();
}

// --- Molette : valeur sous la souris, ou zoom avec Ctrl ---

void Canvas::wheelEvent(QWheelEvent *e)
{
    const int delta = e->angleDelta().y();
    if (e->modifiers() & Qt::ControlModifier) {
        setZoom(zoom_ * std::pow(ZoomStep, delta / qreal(WheelNotch)), e->position());
        return e->accept();
    }
    const QPointF m = toModel(e->position());
    const int i = itemAt(m);
    const int input = i >= 0 ? items_[i].valueAt(m) : -1;
    if (input < 0) {
        wheelAccum_ = 0;
        return e->ignore();  // la vue défile
    }
    e->accept();
    // Crans entiers (un pavé tactile envoie de petites valeurs)
    wheelAccum_ += delta;
    const int steps = wheelAccum_ / WheelNotch;
    wheelAccum_ -= steps * WheelNotch;
    if (steps != 0)
        stepValue(items_[i].block, input, steps);
}

void Canvas::stepValue(Block *block, int input, int steps)
{
    if (edited_)
        commitEdit();
    const Param param = block->spec().param;
    double &value = block->inputs[input].value;
    double v = value;
    for (int n = 0; n < qAbs(steps); ++n) {
        double step = 1;
        if (param == Param::Seconds)  // 0,1 s jusqu'à 2 s, puis 0,5 s
            step = (steps > 0 ? v >= 2 : v > 2) ? 0.5 : 0.1;
        v = clampValue(param, v + (steps > 0 ? step : -step));
    }
    if (v == value)
        return;
    value = v;
    relayout();
    wheelCommit_.start();
}

// --- Zoom ---

QScrollArea *Canvas::scrollArea() const
{
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(w))
            return area;
    }
    return nullptr;
}

QPointF Canvas::viewCenter() const
{
    const QScrollArea *area = scrollArea();
    if (!area)
        return rect().center();
    return QPointF(area->horizontalScrollBar()->value() + area->viewport()->width() / 2.0,
                   area->verticalScrollBar()->value() + area->viewport()->height() / 2.0);
}

void Canvas::zoomIn()
{
    setZoom(zoom_ * ZoomStep, viewCenter());
}

void Canvas::zoomOut()
{
    setZoom(zoom_ / ZoomStep, viewCenter());
}

void Canvas::resetZoom()
{
    setZoom(1, viewCenter());
}

void Canvas::setZoom(qreal zoom, const QPointF &anchor)
{
    zoom = qBound(MinZoom, zoom, MaxZoom);
    if (qAbs(zoom - zoom_) < 1e-3)
        return;
    commitEdit();  // l'éditeur est placé pour l'ancien zoom
    QScrollArea *area = scrollArea();
    const QPointF model = toModel(anchor);
    const QPointF scroll = area ? QPointF(area->horizontalScrollBar()->value(), area->verticalScrollBar()->value())
                                : QPointF();
    zoom_ = zoom;
    relayout();
    if (area) {
        // Nouvelle taille appliquée tout de suite, pour pouvoir régler les barres de défilement
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        const QPointF target = model * zoom_ - (anchor - scroll);
        area->horizontalScrollBar()->setValue(qRound(target.x()));
        area->verticalScrollBar()->setValue(qRound(target.y()));
    }
    emit zoomChanged(zoom_);
}

// --- Menu du fond ---

void Canvas::arrangeScripts()
{
    commitEdit();
    std::vector<Script *> order;
    for (const auto &s : scripts_) {
        if (s->top)
            order.push_back(s.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Script *a, const Script *b) {
        return a->pos.y() != b->pos.y() ? a->pos.y() < b->pos.y() : a->pos.x() < b->pos.x();
    });
    qreal y = FirstScript.y();
    for (Script *s : order) {
        s->pos = QPointF(FirstScript.x(), y);
        QList<Item> tmp;
        blockview::layout(s->top, s->pos, tmp);
        y += blockview::bounds(tmp).height() + ArrangeGap;
    }
    relayout();
    edited();
}

void Canvas::removeAll()
{
    commitEdit();
    scripts_.clear();
    relayout();
    edited();
}

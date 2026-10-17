#include "canvas.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
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
constexpr qreal MinZoom = 0.25;
constexpr qreal MaxZoom = 2;
constexpr qreal ZoomStep = 1.25;
constexpr int WheelNotch = 120;
constexpr int WheelCommitMs = 600;
constexpr qreal ArrangeGap = 24;
constexpr int EditText = -1;      // editedInput_ : saisie du texte libre %t
constexpr int MaxTextLength = 40;
const QSizeF CommentSize(200, 110);
const QSizeF CommentMin(90, 50);
constexpr qreal CommentHeader = 18;  // bandeau du haut (on saisit la note par là ou ailleurs)
constexpr qreal CommentGrip = 14;    // coin bas-droit : redimensionner
const QColor CommentColor(0xFE, 0xF4, 0x9C);
const QColor CommentHeaderColor(0xF2, 0xE4, 0x7A);
const QPointF FirstScript(40, 40);
const QString Format = QStringLiteral("batmobile-scratch");
const QString ClipboardType = QStringLiteral("application/x-batmobile-scripts");
const QPointF PasteOffset(24, 24);

// Visite en profondeur : le bloc, ses entrées, ses corps, puis les suivants
void forEachBlock(Block *b, const std::function<void(Block *)> &f)
{
    for (; b; b = b->next.get()) {
        f(b);
        for (Input &in : b->inputs)
            forEachBlock(in.reporter.get(), f);
        forEachBlock(b->body.get(), f);
        forEachBlock(b->body2.get(), f);
    }
}

// Le bloc, ceux qui le suivent, et tout ce qu'ils contiennent
void collectStack(const Block *b, QSet<const Block *> *out)
{
    for (; b; b = b->next.get()) {
        out->insert(b);
        for (const Input &in : b->inputs)
            collectStack(in.reporter.get(), out);
        collectStack(b->body.get(), out);
        collectStack(b->body2.get(), out);
    }
}

}  // namespace

Canvas::Canvas(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    editor_ = new QLineEdit(this);
    editor_->setAlignment(Qt::AlignCenter);
    editor_->hide();
    connect(editor_, &QLineEdit::editingFinished, this, &Canvas::commitEdit);
    noteEditor_ = new QPlainTextEdit(this);
    noteEditor_->hide();
    noteEditor_->installEventFilter(this);
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
    comments_.clear();
    selection_.clear();
    addScript(FirstScript, std::make_unique<Block>(Kind::Start));
    setVariables({});
    relayout();
    updateProcedures();
    resetHistory();
}

QJsonObject Canvas::save() const
{
    // Attache d'un commentaire : indice de son bloc dans allBlocks() (les ids ne sont pas enregistrés)
    QHash<quint64, int> indexOf;
    const QList<Block *> blocks = allBlocks();
    for (int i = 0; i < blocks.size(); ++i)
        indexOf[blocks[i]->id] = i;
    QJsonArray comments;
    for (const auto &c : comments_) {
        QJsonObject o{
            {QStringLiteral("x"), c->rect.x()},
            {QStringLiteral("y"), c->rect.y()},
            {QStringLiteral("w"), c->rect.width()},
            {QStringLiteral("h"), c->rect.height()},
            {QStringLiteral("text"), c->text},
        };
        if (const auto it = indexOf.constFind(c->blockId); c->blockId && it != indexOf.cend()) {
            o[QStringLiteral("block")] = *it;
            o[QStringLiteral("dx")] = c->offset.x();
            o[QStringLiteral("dy")] = c->offset.y();
        }
        comments.append(o);
    }
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
        {QStringLiteral("comments"), comments},
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
    comments_.clear();
    selection_.clear();
    for (const QJsonValue &v : o[QStringLiteral("scripts")].toArray()) {
        const QJsonObject s = v.toObject();
        if (BlockPtr top = blocksFromJson(s[QStringLiteral("blocks")].toArray()))
            addScript(QPointF(s[QStringLiteral("x")].toDouble(), s[QStringLiteral("y")].toDouble()), std::move(top));
    }
    const QList<Block *> blocks = allBlocks();
    for (const QJsonValue &v : o[QStringLiteral("comments")].toArray()) {
        const QJsonObject c = v.toObject();
        auto comment = std::make_unique<Comment>();
        comment->rect = QRectF(c[QStringLiteral("x")].toDouble(), c[QStringLiteral("y")].toDouble(),
                               qMax(CommentMin.width(), c[QStringLiteral("w")].toDouble(CommentSize.width())),
                               qMax(CommentMin.height(), c[QStringLiteral("h")].toDouble(CommentSize.height())));
        comment->text = c[QStringLiteral("text")].toString();
        if (const int i = c[QStringLiteral("block")].toInt(-1); i >= 0 && i < blocks.size()) {
            comment->blockId = blocks[i]->id;
            comment->offset = QPointF(c[QStringLiteral("dx")].toDouble(), c[QStringLiteral("dy")].toDouble());
        }
        comments_.push_back(std::move(comment));
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
    QList<Procedure> list;
    for (const Script *s : order) {
        if (std::none_of(list.cbegin(), list.cend(), [s](const Procedure &p) { return p.name == s->top->name; }))
            list.append({s->top->name, s->top->params});
    }
    if (list == procedures_)
        return;
    procedures_ = list;
    emit proceduresChanged(procedures_);
}

const Block *Canvas::procedure(const QString &name) const
{
    return topScript([&name](const Block *top) { return top->kind == Kind::Define && top->name == name; });
}

bool Canvas::addProcedure(const QString &name, const QStringList &params)
{
    if (name.isEmpty() || procedure(name))
        return false;
    commitEdit();
    // Sous tous les scripts, à gauche
    const QRectF r = blockview::bounds(items_);
    auto define = std::make_unique<Block>(Kind::Define);
    define->name = name;
    define->params = params.mid(0, MaxParams);
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
    placeAttachedComments();
    QRectF r = blockview::bounds(items_);
    for (const auto &c : comments_)
        r |= c->rect;
    if (!r.isNull() && (r.left() < 0 || r.top() < 0)) {
        // Un script ou un commentaire remonté au-dessus du bord : tout redescendre
        const QPointF shift(qMax(0.0, Margin - r.left()), qMax(0.0, Margin - r.top()));
        for (const auto &s : scripts_)
            s->pos += shift;
        for (const auto &c : comments_)
            c->rect.translate(shift);
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
    if (const Shape shape = stack->spec().shape; isReporter(shape)) {
        // Reporter : dans une entrée libre de la bonne forme, près de sa pastille
        QList<Item> tmp;
        const qreal h = blockview::layout(stack, QPointF(), tmp);
        for (const Item &it : items_) {
            if (!acceptsShape(it.block->spec().param, shape))
                continue;
            for (int i = 0; i < 2; ++i) {
                if (!it.value[i].isNull())
                    consider(&it.block->inputs[i].reporter, nullptr, QPointF(it.value[i].left(), it.value[i].center().y()),
                             topLeft + QPointF(0, h / 2));
            }
        }
        // Texte libre : accepte tous les reporters
        for (const Item &it : items_) {
            if (!it.textField.isNull())
                consider(&it.block->inputs[TextInput].reporter, nullptr,
                         QPointF(it.textField.left(), it.textField.center().y()), topLeft + QPointF(0, h / 2));
        }
        return best;
    }
    // Une boucle sans fin en bas de la pile : rien ne peut la suivre
    const bool cap = lastBlock(stack.get())->spec().shape == Shape::CapC;

    if (stack->spec().shape != Shape::Hat) {
        for (const Item &it : items_) {
            const Shape s = it.block->spec().shape;
            if (isReporter(s))
                continue;
            if (s != Shape::CapC && !(cap && it.block->next))
                consider(&it.block->next, nullptr, it.pos + QPointF(0, it.height), topLeft);
            if (hasBody(s) && !(cap && it.block->body))
                consider(&it.block->body, nullptr, it.pos + QPointF(blockview::ArmW, it.rowH), topLeft);
            if (hasElse(s) && !(cap && it.block->body2))
                consider(&it.block->body2, nullptr, it.pos + QPointF(blockview::ArmW, it.elseTop), topLeft);
        }
    }
    if (!cap) {
        QList<Item> tmp;
        const qreal h = blockview::layout(stack, QPointF(), tmp);
        for (const auto &s : scripts_) {
            const Shape top = s->top ? s->top->spec().shape : Shape::Hat;
            if (top != Shape::Hat && !isReporter(top))
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

    // Sélection : halo bleu sous les blocs sélectionnés et ce qu'ils entraînent
    QSet<const Block *> selected;
    for (const Block *b : selectionRoots())
        collectStack(b, &selected);
    if (!selected.isEmpty()) {
        p.setPen(QPen(QColor(0x66, 0xCC, 0xFF), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        for (const Item &it : items_) {
            if (selected.contains(it.block))
                p.drawPath(it.path);
        }
    }
    for (const Item &it : items_)
        blockview::paint(p, it, it.block == current_, ghostBlocks_.contains(it.block));
    for (const Item &it : ghostItems_)
        blockview::paint(p, it, false, true);

    paintComments(p);
}

void Canvas::mousePressEvent(QMouseEvent *e)
{
    commitEdit();
    pressItem_ = -1;
    pressComment_ = nullptr;
    pressPos_ = toModel(e->position());
    if (e->button() == Qt::LeftButton) {
        // Commentaire : au premier plan, puis déplacé ou redimensionné
        if (const int c = commentAt(pressPos_); c >= 0) {
            std::rotate(comments_.begin() + c, comments_.begin() + c + 1, comments_.end());
            pressComment_ = comments_.back().get();
            commentStart_ = pressComment_->rect;
            const QPointF corner = commentStart_.bottomRight();
            commentAction_ = (corner - pressPos_).x() < CommentGrip && (corner - pressPos_).y() < CommentGrip
                                 ? CommentAction::Resize
                                 : CommentAction::None;  // Move une fois le seuil de glisser dépassé
            if (commentAction_ == CommentAction::Resize)
                setCursor(Qt::SizeFDiagCursor);
            update();
            return;
        }
        pressItem_ = itemAt(pressPos_);
        // Sélection : clic = ce bloc, Ctrl+clic = ajouter / retirer, fond = rien
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        if (pressItem_ < 0) {
            if (!ctrl)
                selection_.clear();
        } else if (const quint64 id = items_[pressItem_].block->id; ctrl) {
            if (!selection_.removeOne(id))
                selection_.append(id);
        } else if (!selection_.contains(id)) {
            selection_ = {id};
        }
        update();
    }
    // Fond saisi (ou bouton du milieu) : déplacement de la vue
    if ((e->button() == Qt::LeftButton && pressItem_ < 0) || e->button() == Qt::MiddleButton) {
        panning_ = true;
        panLast_ = e->globalPosition();
        setCursor(Qt::ClosedHandCursor);
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *e)
{
    if (pressComment_ && (e->buttons() & Qt::LeftButton)) {
        const QPointF delta = toModel(e->position()) - pressPos_;
        if (commentAction_ == CommentAction::None && (delta * zoom_).manhattanLength() >= QApplication::startDragDistance()) {
            commentAction_ = CommentAction::Move;
            setCursor(Qt::ClosedHandCursor);
        }
        if (commentAction_ == CommentAction::Move)
            pressComment_->rect.moveTopLeft(commentStart_.topLeft() + delta);
        else if (commentAction_ == CommentAction::Resize)
            pressComment_->rect.setSize(QSizeF(qMax(CommentMin.width(), commentStart_.width() + delta.x()),
                                               qMax(CommentMin.height(), commentStart_.height() + delta.y())));
        update();
        return;
    }
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
    if (Comment *c = std::exchange(pressComment_, nullptr)) {
        unsetCursor();
        if (commentAction_ == CommentAction::None) {
            editComment(c);  // simple clic : écrire
        } else {
            // Attachée : la nouvelle place est gardée par rapport au bloc
            if (const int i = itemOf(c->blockId); c->blockId && i >= 0)
                c->offset = c->rect.topLeft() - items_[i].pos;
            relayout();  // déplacé : la zone peut s'agrandir
        }
        commentAction_ = CommentAction::None;
        edited();  // sans effet si rien n'a bougé
        return;
    }
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
    if (const int input = items_[i].valueAt(m); input >= 0 && items_[i].block->spec().param != Param::Bool)
        return editValue(i, input);
    if (!items_[i].textField.isNull() && items_[i].textField.contains(m))
        return editValue(i, EditText);
    if (!items_[i].var.isNull() && items_[i].var.contains(m))
        return chooseVariable(i);
    if (!running_)
        emit scriptClicked(itemScripts_[i]->top.get());
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QPointF m = toModel(e->position());
    const int i = commentAt(m) >= 0 ? -1 : itemAt(m);
    if (e->button() != Qt::LeftButton || i < 0 || running_
        || (items_[i].valueAt(m) >= 0 && items_[i].block->spec().param != Param::Bool)
        || (!items_[i].var.isNull() && items_[i].var.contains(m))
        || (!items_[i].textField.isNull() && items_[i].textField.contains(m)))
        return mousePressEvent(e);
    pressItem_ = -1;  // le relâchement qui suit n'est pas un second clic
    emit scriptDoubleClicked(itemScripts_[i]->top.get());
}

void Canvas::contextMenuEvent(QContextMenuEvent *e)
{
    commitEdit();
    const QPointF m = toModel(e->pos());
    if (const int c = commentAt(m); c >= 0) {
        // Les commentaires restent modifiables pendant l'exécution
        QMenu menu(this);
        QAction *detach = comments_[c]->blockId ? menu.addAction(QStringLiteral("Détacher du bloc")) : nullptr;
        QAction *del = menu.addAction(QStringLiteral("Supprimer le commentaire"));
        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == del) {
            removeComment(c);
        } else if (chosen && chosen == detach) {
            comments_[c]->blockId = 0;
            update();
            edited();
        }
        return;
    }
    if (running_)
        return;
    const int i = itemAt(m);
    if (i < 0) {
        // Menu du fond
        QMenu menu(this);
        QAction *note = menu.addAction(QStringLiteral("Ajouter un commentaire"));
        QAction *pasteAction = menu.addAction(QStringLiteral("Coller"));
        pasteAction->setEnabled(QApplication::clipboard()->mimeData()->hasFormat(ClipboardType));
        menu.addSeparator();
        QAction *arrange = menu.addAction(QStringLiteral("Ranger les scripts"));
        QAction *all = menu.addAction(QStringLiteral("Tout supprimer"));
        arrange->setEnabled(!items_.isEmpty());
        all->setEnabled(!items_.isEmpty() || !comments_.empty());
        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == note)
            addComment(m);
        else if (chosen == pasteAction)
            paste();
        else if (chosen == arrange)
            arrangeScripts();
        else if (chosen == all)
            removeAll();
        return;
    }
    QMenu menu(this);
    QAction *dup = menu.addAction(QStringLiteral("Dupliquer"));
    QAction *copy = menu.addAction(QStringLiteral("Copier"));
    QAction *del = menu.addAction(QStringLiteral("Supprimer le bloc"));
    QAction *note = menu.addAction(QStringLiteral("Ajouter un commentaire"));
    QAction *chosen = menu.exec(e->globalPos());
    if (chosen == copy) {
        if (!selection_.contains(items_[i].block->id))
            selection_ = {items_[i].block->id};
        copySelection();
    } else if (chosen == dup)
        duplicate(i);
    else if (chosen == del)
        removeBlock(i);
    else if (chosen == note)  // à droite du bloc, attaché à lui
        addComment(items_[i].pos + QPointF(items_[i].width + 24, 0), {}, items_[i].block->id);
}

void Canvas::startDrag(int item)
{
    commitEdit();
    // Le bloc part avec tous ceux qui le suivent, comme dans Scratch
    BlockPtr *slot = items_[item].slot;
    const QPointF hot = pressPos_ - items_[item].pos;
    BlockPtr stack = std::move(*slot);
    QSet<const Block *> blocks;
    collectStack(stack.get(), &blocks);
    for (const Block *b : blocks)
        dragIds_.insert(b->id);
    dragStack_ = &stack;
    relayout();
    // Déposée ici, dropEvent reprend la pile d'origine ; sur la palette, elle est jetée
    const Qt::DropAction action = blockview::drag(this, stack, hot, zoom_);
    dragStack_ = nullptr;
    dragIds_.clear();
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
    // Pile venant d'ici : l'originale (mêmes blocs), pas la copie du glisser
    BlockPtr stack = e->source() == this && dragStack_ && *dragStack_ ? std::move(*dragStack_) : std::move(incoming_);
    drop(std::move(stack), toModel(e->position()) - incomingHot_);
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
    if (input == EditText) {
        editor_->setMaxLength(MaxTextLength);
        editor_->setText(it.block->text);
    } else {
        editor_->setMaxLength(32767);
        editor_->setValidator(new QRegularExpressionValidator(QRegularExpression(valuePattern(param)), editor_));
        editor_->setText(formatValue(param, it.block->inputs[input].value));
    }
    editor_->setStyleSheet(QStringLiteral("QLineEdit { background: #fff; color: #222; border: 2px solid #4C97FF;"
                                          " border-radius: %1px; font-weight: bold; font-size: %2px; }")
                               .arg(qRound(12 * zoom_))
                               .arg(qRound(13 * zoom_)));
    const QRectF pill = input == EditText ? it.textField : it.value[input];
    QRect r = QRectF(pill.topLeft() * zoom_, pill.size() * zoom_).toAlignedRect().adjusted(-4, -2, 4, 2);
    r.setWidth(qMax(r.width(), qRound((input == EditText ? 160 : 56) * zoom_)));
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
    commitComment();
    // Valeur changée à la molette : l'étape d'annulation est enregistrée maintenant
    if (wheelCommit_.isActive()) {
        wheelCommit_.stop();
        edited();
    }
    if (!edited_)
        return;
    Block *b = std::exchange(edited_, nullptr);
    editor_->hide();  // déclenche editingFinished : edited_ est déjà remis à zéro
    if (editedInput_ == EditText) {
        if (editor_->text() != b->text) {
            b->text = editor_->text();
            edited();
        }
        return relayout();
    }
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
    // Molette : zoom autour de la souris ; sur un nombre, le change ; Maj+molette (ou défilement
    // horizontal d'un pavé tactile) : la vue défile
    const int delta = e->angleDelta().y();
    if ((e->modifiers() & Qt::ShiftModifier) || delta == 0) {
        wheelAccum_ = 0;
        return e->ignore();
    }
    const QPointF m = toModel(e->position());
    const int i = itemAt(m);
    const int input = i >= 0 && items_[i].block->spec().param != Param::Bool ? items_[i].valueAt(m) : -1;
    if (input < 0 || (e->modifiers() & Qt::ControlModifier)) {
        wheelAccum_ = 0;
        setZoom(zoom_ * std::pow(ZoomStep, delta / qreal(WheelNotch)), e->position());
        return e->accept();
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
        else if (param == Param::Distance)  // 1 cm jusqu'à 20 cm, puis 5 cm
            step = (steps > 0 ? v >= 20 : v > 20) ? 5 : 1;
        else if (param == Param::Angle)
            step = 5;
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
    comments_.clear();
    relayout();
    edited();
}

// --- Commentaires ---

int Canvas::commentAt(const QPointF &pos) const
{
    for (int i = int(comments_.size()) - 1; i >= 0; --i) {
        if (comments_[i]->rect.contains(pos))
            return i;
    }
    return -1;
}

void Canvas::addComment(const QPointF &pos, const QString &text, quint64 blockId)
{
    commitEdit();
    auto c = std::make_unique<Comment>();
    c->rect = QRectF(QPointF(qMax(Margin, pos.x()), qMax(Margin, pos.y())), CommentSize);
    c->text = text;
    if (const int i = itemOf(blockId); blockId && i >= 0) {
        c->blockId = blockId;
        c->offset = c->rect.topLeft() - items_[i].pos;
    }
    Comment *added = c.get();
    comments_.push_back(std::move(c));
    relayout();
    edited();
    if (text.isEmpty())
        editComment(added);  // prêt à écrire
}

void Canvas::removeComment(int index)
{
    commitEdit();
    comments_.erase(comments_.begin() + index);
    relayout();
    edited();
}

void Canvas::editComment(Comment *c)
{
    editedComment_ = c;
    const QRectF body = c->rect.adjusted(2, CommentHeader, -2, -2);
    noteEditor_->setStyleSheet(QStringLiteral("QPlainTextEdit { background: #FEF49C; color: #574D1E; border: none;"
                                              " font-size: %1px; }")
                                   .arg(qRound(12 * zoom_)));
    noteEditor_->setGeometry(QRectF(body.topLeft() * zoom_, body.size() * zoom_).toAlignedRect());
    noteEditor_->setPlainText(c->text);
    noteEditor_->moveCursor(QTextCursor::End);
    noteEditor_->show();
    noteEditor_->setFocus();
    update();
}

void Canvas::commitComment()
{
    Comment *c = std::exchange(editedComment_, nullptr);
    if (!c)
        return;
    noteEditor_->hide();  // la perte de focus qui suit ne fait plus rien
    update();
    const QString text = noteEditor_->toPlainText();
    if (text == c->text)
        return;
    c->text = text;
    edited();
}

bool Canvas::eventFilter(QObject *o, QEvent *e)
{
    if (o == noteEditor_ && e->type() == QEvent::FocusOut)
        commitComment();
    return QWidget::eventFilter(o, e);
}

// --- Sélection et presse-papiers ---

int Canvas::itemOf(quint64 id) const
{
    for (int i = 0; i < items_.size(); ++i) {
        if (items_[i].block->id == id)
            return i;
    }
    return -1;
}

QList<const Block *> Canvas::selectionRoots() const
{
    QList<const Block *> roots;
    for (const quint64 id : selection_) {
        if (const int i = itemOf(id); i >= 0)
            roots.append(items_[i].block);
    }
    // Un bloc déjà entraîné par un autre bloc sélectionné n'est pas compté deux fois
    QList<const Block *> out;
    for (const Block *b : roots) {
        const bool inside = std::any_of(roots.cbegin(), roots.cend(), [b](const Block *other) {
            if (other == b)
                return false;
            QSet<const Block *> stack;
            collectStack(other, &stack);
            return stack.contains(b);
        });
        if (!inside)
            out.append(b);
    }
    return out;
}

void Canvas::copySelection()
{
    commitEdit();
    const QList<const Block *> roots = selectionRoots();
    if (roots.isEmpty())
        return;
    // Positions relatives au coin haut gauche de la sélection
    QPointF origin(1e9, 1e9);
    for (const Block *b : roots) {
        const QPointF pos = items_[itemOf(b->id)].pos;
        origin = QPointF(qMin(origin.x(), pos.x()), qMin(origin.y(), pos.y()));
    }
    QJsonArray scripts;
    for (const Block *b : roots) {
        const QPointF pos = items_[itemOf(b->id)].pos - origin;
        // Le bloc et ceux qui le suivent (comme Dupliquer)
        scripts.append(QJsonObject{
            {QStringLiteral("x"), pos.x()},
            {QStringLiteral("y"), pos.y()},
            {QStringLiteral("blocks"), blocksToJson(b)},
        });
    }
    auto *mime = new QMimeData;
    mime->setData(ClipboardType, QJsonDocument(QJsonObject{{QStringLiteral("scripts"), scripts}}).toJson());
    QApplication::clipboard()->setMimeData(mime);
}

void Canvas::paste()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (running_ || !mime || !mime->hasFormat(ClipboardType))
        return;
    commitEdit();
    // Sous la souris si elle est sur la zone visible, sinon près du centre de la vue
    const QPointF mouse = mapFromGlobal(QCursor::pos());
    const QPointF target = visibleRegion().contains(mouse.toPoint()) ? toModel(mouse)
                                                                       : toModel(viewCenter()) - QPointF(100, 60);
    selection_.clear();
    const QJsonArray scripts = QJsonDocument::fromJson(mime->data(ClipboardType)).object()[QStringLiteral("scripts")].toArray();
    for (const QJsonValue &v : scripts) {
        const QJsonObject s = v.toObject();
        if (BlockPtr top = blocksFromJson(s[QStringLiteral("blocks")].toArray())) {
            selection_.append(top->id);  // les blocs collés restent sélectionnés
            addScript(target + QPointF(s[QStringLiteral("x")].toDouble(), s[QStringLiteral("y")].toDouble()),
                      std::move(top));
        }
    }
    setVariables(variables_);  // variables utilisées par les blocs collés
    relayout();
    edited();
}

void Canvas::deleteSelection()
{
    if (running_)
        return;
    commitEdit();
    bool removed = false;
    for (const Block *b : selectionRoots()) {
        const int i = itemOf(b->id);
        if (i < 0)
            continue;
        BlockPtr gone = std::move(*items_[i].slot);  // le bloc et ceux qui le suivent
        removed = true;
        relayout();  // les slots des blocs restants restent valides, les Item sont recalculés
    }
    selection_.clear();
    if (!removed)
        return update();
    pruneScripts();
    relayout();
    edited();
}

void Canvas::cutSelection()
{
    if (running_)
        return;
    copySelection();
    deleteSelection();
}

void Canvas::selectAll()
{
    selection_.clear();
    for (const auto &s : scripts_) {
        if (s->top)
            selection_.append(s->top->id);
    }
    update();
}

QList<Block *> Canvas::allBlocks() const
{
    QList<Block *> out;
    for (const auto &s : scripts_)
        forEachBlock(s->top.get(), [&out](Block *b) { out.append(b); });
    return out;
}

void Canvas::placeAttachedComments()
{
    // Une note suit son bloc ; si le bloc a disparu (supprimé), elle disparaît avec lui, sauf
    // pendant que le bloc est en train d'être glissé
    for (auto it = comments_.begin(); it != comments_.end();) {
        Comment *c = it->get();
        if (c->blockId) {
            if (const int i = itemOf(c->blockId); i >= 0) {
                c->rect.moveTopLeft(items_[i].pos + c->offset);
            } else if (!dragIds_.contains(c->blockId)) {
                if (editedComment_ == c) {
                    editedComment_ = nullptr;
                    noteEditor_->hide();
                }
                if (pressComment_ == c)
                    pressComment_ = nullptr;
                it = comments_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void Canvas::paintComments(QPainter &p) const
{
    // Liaison d'un commentaire attaché : de la droite de la ligne du bloc au bandeau de la note
    p.setPen(QPen(QColor(0xD9, 0xC9, 0x5C), 1.5));
    for (const auto &c : comments_) {
        if (const int i = itemOf(c->blockId); c->blockId && i >= 0) {
            const Item &it = items_[i];
            const qreal rowTop = it.pos.y() + (it.block->spec().shape == Shape::Hat ? blockview::Dome : 0);
            p.drawLine(QPointF(it.pos.x() + it.width, rowTop + it.rowH / 2),
                       QPointF(c->rect.left(), c->rect.top() + CommentHeader / 2));
        }
    }

    // Commentaires, au-dessus des blocs
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    for (const auto &c : comments_) {
        const QRectF r = c->rect;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRoundedRect(r.translated(2, 3), 6, 6);  // ombre
        p.setPen(QPen(QColor(0xD9, 0xC9, 0x5C), 1));
        p.setBrush(CommentColor);
        p.drawRoundedRect(r, 6, 6);
        p.setPen(Qt::NoPen);
        p.setBrush(CommentHeaderColor);
        QPainterPath header;
        header.addRoundedRect(QRectF(r.left(), r.top(), r.width(), CommentHeader + 6), 6, 6);
        p.drawPath(header.intersected([&] {
            QPainterPath clip;
            clip.addRect(QRectF(r.left(), r.top(), r.width(), CommentHeader));
            return clip;
        }()));
        if (c.get() != editedComment_) {
            p.setPen(QColor(0x57, 0x4D, 0x1E));
            p.save();
            const QRectF body = r.adjusted(6, CommentHeader + 2, -6, -4);
            p.setClipRect(body);
            p.drawText(body, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                       c->text.isEmpty() ? QStringLiteral("(clic pour écrire)") : c->text);
            p.restore();
        }
        // Poignée de redimensionnement
        p.setPen(QPen(QColor(0xB8, 0xA8, 0x45), 1.5));
        for (qreal d = 4; d <= 10; d += 3)
            p.drawLine(QPointF(r.right() - d, r.bottom() - 2), QPointF(r.right() - 2, r.bottom() - d));
    }
}

QImage Canvas::renderImage(qreal scale) const
{
    // Scripts et commentaires sur fond blanc, sans sélection ni bloc en cours
    QRectF r = blockview::bounds(items_);
    for (const auto &c : comments_)
        r |= c->rect.adjusted(0, 0, 2, 3);  // ombre
    if (r.isNull())
        return {};
    r.adjust(-20, -20, 20, 20);
    QImage image((r.size() * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    p.translate(-r.topLeft());
    for (const Item &it : items_)
        blockview::paint(p, it);
    paintComments(p);
    return image;
}

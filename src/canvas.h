#pragma once

#include "blockview.h"

#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QLineEdit;
class QPlainTextEdit;
class QScrollArea;

// Zone de programme : scripts posés librement, blocs emboîtés par glisser-déposer
class Canvas : public QWidget
{
    Q_OBJECT

public:
    explicit Canvas(QWidget *parent = nullptr);

    void clear();  // un seul bloc « quand ▶ est cliqué »
    QJsonObject save() const;
    bool load(const QJsonObject &o);  // remet l'historique à zéro

    // Annuler / rétablir (sauf pendant l'exécution)
    bool canUndo() const { return !running_ && historyIndex_ > 0; }
    bool canRedo() const { return !running_ && historyIndex_ < history_.size() - 1; }
    void undo();
    void redo();

    // Différent du dernier état enregistré (ou chargé)
    bool isModified() const;
    QJsonObject savedState() const { return saved_; }
    void setSavedState(const QJsonObject &state) { saved_ = state; }
    void markSaved() { saved_ = save(); }

    // Zoom (50 à 200 %), autour du centre de la vue
    qreal zoom() const { return zoom_; }
    void zoomIn();
    void zoomOut();
    void resetZoom();

    // Menu du fond (annulables)
    void arrangeScripts();  // en colonne, dans l'ordre de haut en bas
    void removeAll();       // scripts et commentaires
    // pos : coordonnées du programme ; blockId : bloc auquel la note est attachée (0 = libre)
    void addComment(const QPointF &pos, const QString &text = {}, quint64 blockId = 0);

    // Sélection (un bloc et ceux qui le suivent) et presse-papiers
    void copySelection();
    void cutSelection();
    void paste();
    void deleteSelection();
    void selectAll();

    // Variables du programme (enregistrées avec lui, annulables)
    QStringList variables() const { return variables_; }
    bool addVariable(const QString &name);  // false si le nom existe déjà
    bool removeVariable(const QString &name);  // false si un bloc l'utilise encore

    // Mes blocs : scripts « définir nom »
    QList<Procedure> procedures() const { return procedures_; }
    const Block *procedure(const QString &name) const;  // le plus haut s'il y en a plusieurs
    // Pose « définir nom (paramètres) » sous les scripts ; false si déjà défini
    bool addProcedure(const QString &name, const QStringList &params = {});

    // Script le plus haut commençant par « quand ▶ est cliqué » (nullptr s'il n'y en a pas)
    const Block *startScript() const;
    // Script le plus haut commençant par « quand la touche … » pour cette touche (indice keyIndex)
    const Block *keyScript(int key) const;

    // Pendant l'exécution : pas de modification de la structure (le Runner garde des pointeurs)
    void setRunning(bool on);
    void setCurrent(const Block *block);

    // Aperçu inséré dans le programme pendant un glisser-déposer (à ne pas sauvegarder)
    bool hasGhost() const { return ghostSlot_ != nullptr; }

signals:
    // Clic / double-clic sur un script (la fenêtre décide de l'exécuter)
    void scriptClicked(const Block *first);
    void scriptDoubleClicked(const Block *first);
    void changed();
    void historyChanged();
    void variablesChanged(const QStringList &names);
    void proceduresChanged(const QList<Procedure> &procedures);
    void zoomChanged(qreal zoom);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;  // fin de saisie d'un commentaire

private:
    // Note jaune, libre ou attachée à un bloc (elle suit alors son script, à offset de lui)
    struct Comment {
        QRectF rect;
        QString text;
        quint64 blockId = 0;
        QPointF offset;
    };
    enum class CommentAction { None, Move, Resize };
    struct Script {
        QPointF pos;
        BlockPtr top;
    };

    // Point d'accroche d'une pile déposée
    struct Snap {
        BlockPtr *slot = nullptr;  // insérée à cet endroit (après un bloc, dans une boucle ou une entrée)
        Script *before = nullptr;  // ou placée au-dessus de ce script
        QPointF at;
        bool valid() const { return slot || before; }
    };

    void edited();  // enregistre l'état dans l'historique et émet changed
    void resetHistory();
    void restore(const QJsonObject &o);
    void relayout();
    const Block *topScript(const std::function<bool(const Block *)> &match) const;
    QPointF toModel(const QPointF &widgetPos) const { return widgetPos / zoom_; }
    QScrollArea *scrollArea() const;
    QPointF viewCenter() const;
    void setZoom(qreal zoom, const QPointF &anchor);  // anchor (widget) reste sous la souris
    void stepValue(Block *block, int input, int steps);
    int itemAt(const QPointF &pos) const;
    Snap findSnap(BlockPtr &stack, const QPointF &topLeft) const;
    void showGhost(const Snap &snap);
    void removeGhost();  // incoming_ redevient propriétaire de la pile
    void endDrop();
    void drop(BlockPtr stack, const QPointF &topLeft);
    void addScript(const QPointF &pos, BlockPtr top);
    void pruneScripts();  // jamais pendant un glisser : un Item peut pointer sur Script::top
    void startDrag(int item);
    void editValue(int item, int input);
    void chooseKey(int item);
    void chooseVariable(int item);
    void setVariables(QStringList names);  // complétée par les noms utilisés dans les scripts
    void updateProcedures();
    void commitEdit();
    void removeBlock(int item);
    int itemOf(quint64 id) const;  // -1 si le bloc n'est plus dans le programme
    QList<Block *> allBlocks() const;  // ordre fixe (enregistrement des attaches de commentaires)
    void placeAttachedComments();      // dans relayout()
    QList<const Block *> selectionRoots() const;  // sans ceux contenus dans un autre bloc sélectionné
    int commentAt(const QPointF &pos) const;  // le plus haut (dessiné en dernier), ou -1
    void editComment(Comment *c);
    void commitComment();
    void removeComment(int index);
    void duplicate(int item);

    std::vector<std::unique_ptr<Script>> scripts_;
    QList<blockview::Item> items_;
    QList<Script *> itemScripts_;  // script de chaque item
    bool running_ = false;
    const Block *current_ = nullptr;
    int pressItem_ = -1;
    QPointF pressPos_;  // coordonnées du programme (sans zoom)
    bool panning_ = false;
    QPointF panLast_;   // position globale
    qreal zoom_ = 1;
    int wheelAccum_ = 0;
    QTimer wheelCommit_;  // regroupe les crans de molette en une seule étape d'annulation
    BlockPtr incoming_;  // pile qui survole la zone pendant un glisser-déposer
    QPointF incomingHot_;
    // Aperçu : la pile survolante est insérée à sa place (les blocs suivants descendent) ;
    // au-dessus d'un script, elle est seulement dessinée (ghostItems_)
    BlockPtr *ghostSlot_ = nullptr;
    Block *ghostTail_ = nullptr;
    QList<blockview::Item> ghostItems_;
    QSet<const Block *> ghostBlocks_;
    QLineEdit *editor_;
    Block *edited_ = nullptr;
    int editedInput_ = 0;
    QStringList variables_;
    QList<Procedure> procedures_;
    std::vector<std::unique_ptr<Comment>> comments_;
    Comment *pressComment_ = nullptr;
    CommentAction commentAction_ = CommentAction::None;
    QRectF commentStart_;     // rectangle au moment de l'appui
    QPlainTextEdit *noteEditor_;
    Comment *editedComment_ = nullptr;
    QList<quint64> selection_;  // ids des blocs sélectionnés
    // Glisser-déposer interne : la pile d'origine est redéposée (ids conservés, ses
    // commentaires la suivent) ; ses blocs, hors du programme pendant le glisser, gardent leurs notes
    BlockPtr *dragStack_ = nullptr;
    QSet<quint64> dragIds_;
    QList<QJsonObject> history_;
    int historyIndex_ = 0;
    QJsonObject saved_;
};

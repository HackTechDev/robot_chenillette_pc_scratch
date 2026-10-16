#pragma once

#include "blockview.h"

#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

class QLineEdit;
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
    void removeAll();

    // Script le plus haut commençant par « quand ▶ est cliqué » (nullptr s'il n'y en a pas)
    const Block *startScript() const;

    // Pendant l'exécution : pas de modification de la structure (le Runner garde des pointeurs)
    void setRunning(bool on);
    void setCurrent(const Block *block);

    // Aperçu inséré dans le programme pendant un glisser-déposer (à ne pas sauvegarder)
    bool hasGhost() const { return ghostSlot_ != nullptr; }

signals:
    void runRequested(const Block *first);  // clic sur un script
    void changed();
    void historyChanged();
    void zoomChanged(qreal zoom);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    struct Script {
        QPointF pos;
        BlockPtr top;
    };

    // Point d'accroche d'une pile déposée
    struct Snap {
        BlockPtr *slot = nullptr;  // insérée à cet endroit (après un bloc ou dans une boucle)
        Script *before = nullptr;  // ou placée au-dessus de ce script
        QPointF at;
        bool valid() const { return slot || before; }
    };

    void edited();  // enregistre l'état dans l'historique et émet changed
    void resetHistory();
    void restore(const QJsonObject &o);
    void relayout();
    QPointF toModel(const QPointF &widgetPos) const { return widgetPos / zoom_; }
    QScrollArea *scrollArea() const;
    QPointF viewCenter() const;
    void setZoom(qreal zoom, const QPointF &anchor);  // anchor (widget) reste sous la souris
    void stepValue(Block *block, int steps);
    int itemAt(const QPointF &pos) const;
    Snap findSnap(BlockPtr &stack, const QPointF &topLeft) const;
    void showGhost(const Snap &snap);
    void removeGhost();  // incoming_ redevient propriétaire de la pile
    void endDrop();
    void drop(BlockPtr stack, const QPointF &topLeft);
    void addScript(const QPointF &pos, BlockPtr top);
    void pruneScripts();  // jamais pendant un glisser : un Item peut pointer sur Script::top
    void startDrag(int item);
    void editValue(int item);
    void commitEdit();
    void removeBlock(int item);
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
    QList<QJsonObject> history_;
    int historyIndex_ = 0;
    QJsonObject saved_;
};

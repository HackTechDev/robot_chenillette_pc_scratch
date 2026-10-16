#pragma once

#include "blockview.h"

#include <QJsonObject>
#include <QWidget>

#include <memory>
#include <vector>

class QLineEdit;

// Zone de programme : scripts posés librement, blocs emboîtés par glisser-déposer
class Canvas : public QWidget
{
    Q_OBJECT

public:
    explicit Canvas(QWidget *parent = nullptr);

    void clear();  // un seul bloc « quand ▶ est cliqué »
    QJsonObject save() const;
    bool load(const QJsonObject &o);

    // Script le plus haut commençant par « quand ▶ est cliqué » (nullptr s'il n'y en a pas)
    const Block *startScript() const;

    // Pendant l'exécution : pas de modification de la structure (le Runner garde des pointeurs)
    void setRunning(bool on);
    void setCurrent(const Block *block);

signals:
    void runRequested(const Block *first);  // clic sur un script
    void changed();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
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

    void relayout();
    int itemAt(const QPointF &pos) const;
    Snap findSnap(BlockPtr &stack, const QPointF &topLeft) const;
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
    QPointF pressPos_;
    BlockPtr incoming_;  // pile qui survole la zone pendant un glisser-déposer
    QPointF incomingHot_;
    Snap snap_;
    QLineEdit *editor_;
    Block *edited_ = nullptr;
};

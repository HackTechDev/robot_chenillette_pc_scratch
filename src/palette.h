#pragma once

#include "blockview.h"

#include <QWidget>

#include <vector>

// Palette de blocs, rangés par catégorie : on en tire une copie vers la zone de
// programme. Une pile déposée sur la palette est supprimée.
class Palette : public QWidget
{
    Q_OBJECT

public:
    explicit Palette(QWidget *parent = nullptr);

    void setLocked(bool on);  // pas de glisser pendant l'exécution

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    struct Heading {
        QString name;
        QColor color;
        qreal y;
    };

    std::vector<BlockPtr> templates_;
    QList<blockview::Item> items_;
    QList<Heading> headings_;
    int pressItem_ = -1;
    QPointF pressPos_;
    bool locked_ = false;
    bool trashHover_ = false;
};

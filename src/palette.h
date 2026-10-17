#pragma once

#include "blockview.h"

#include <QSet>
#include <QWidget>

#include <vector>

// Palette de blocs, rangés par catégorie : on en tire une copie vers la zone de
// programme. Une pile déposée sur la palette est supprimée. La section Variables
// suit les variables du programme.
class Palette : public QWidget
{
    Q_OBJECT

public:
    explicit Palette(QWidget *parent = nullptr);

    void setLocked(bool on);  // pas de glisser pendant l'exécution
    void setVariables(const QStringList &names);
    void setProcedures(const QList<Procedure> &procedures);  // blocs personnalisés définis
    // Recherche : seuls les blocs dont le texte contient filter (sans accents ni majuscules)
    void setFilter(const QString &filter);
    void toggleSection(const QString &name);  // replier / déplier une catégorie

signals:
    void createVariableRequested();
    void createBlockRequested();
    void deleteVariableRequested(const QString &name);
    void widthChanged(int width);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    struct Heading {
        QString name;
        QColor color;
        qreal y;
        bool collapsed = false;
    };

    enum ButtonId { CreateVariable, CreateBlock };
    struct Button {
        QRectF rect;
        QString text;
        ButtonId id = CreateVariable;
    };

    void rebuild();

    QStringList variables_;
    QList<Procedure> procedures_;
    std::vector<BlockPtr> templates_;
    QList<blockview::Item> items_;
    QList<Heading> headings_;
    QList<Button> buttons_;
    QString filter_;
    QSet<QString> collapsed_;  // catégories repliées
    int pressItem_ = -1;
    QPointF pressPos_;
    bool locked_ = false;
    bool trashHover_ = false;
};

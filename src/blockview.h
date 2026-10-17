#pragma once

#include "blocks.h"

#include <QFont>
#include <QList>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

#include <array>

class QMimeData;
class QPainter;
class QWidget;

// Géométrie et dessin des blocs, partagés par la palette et la zone de programme
namespace blockview {

inline constexpr qreal Dome = 18;     // arrondi au-dessus d'un chapeau
inline constexpr qreal RowH = 40;     // hauteur minimale d'un bloc (ou de la tête d'une boucle)
inline constexpr qreal ReporterH = 30; // hauteur minimale d'un reporter
inline constexpr qreal ArmW = 16;     // bras gauche d'une boucle
inline constexpr qreal MinInner = 24; // corps de boucle vide
inline constexpr qreal BottomH = 24;  // bas d'une boucle
inline constexpr qreal ElseH = 32;    // ligne « sinon » entre les deux corps

inline const QString MimeType = QStringLiteral("application/x-batmobile-blocks");

// Bloc placé : position, contour et zones de texte
struct Item {
    Block *block = nullptr;
    BlockPtr *slot = nullptr;  // pointeur qui possède ce bloc (pour le détacher)
    QPointF pos;               // coin haut gauche (haut de l'arrondi pour un chapeau)
    qreal width = 0;
    qreal height = 0;          // le bloc suivant commence à pos.y + height
    qreal rowH = 0;            // hauteur de la ligne de texte (plus haute avec des reporters)
    qreal elseTop = 0;         // « sinon » : début du second corps, depuis pos.y
    QPainterPath path;
    struct Text {
        QRectF rect;
        QString text;
    };
    QList<Text> texts;
    std::array<QRectF, 2> value;  // pastille de chaque entrée sans reporter (vide sinon)
    QRectF var;                   // liste des variables (vide s'il n'y en a pas)

    int valueAt(const QPointF &p) const;  // entrée dont la pastille contient p, ou -1
};

QFont font();

// Place la pile qui commence à first (blocs suivants et corps des boucles compris),
// ajoute les blocs à out dans l'ordre de dessin et renvoie la hauteur de la pile.
qreal layout(BlockPtr &first, QPointF pos, QList<Item> &out);
QRectF bounds(const QList<Item> &items);
// ghost : aperçu gris de l'endroit où une pile va être déposée
void paint(QPainter &p, const Item &item, bool highlight = false, bool ghost = false);

// Glisser-déposer d'une pile : hot = point saisi, relatif au coin haut gauche de la pile
// scale : zoom de la zone d'origine (taille de l'image glissée)
Qt::DropAction drag(QWidget *source, BlockPtr &stack, const QPointF &hot, qreal scale = 1);
BlockPtr fromMime(const QMimeData *mime, QPointF *hot);

}  // namespace blockview

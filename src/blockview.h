#pragma once

#include "blocks.h"

#include <QFont>
#include <QList>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

class QMimeData;
class QPainter;
class QWidget;

// Géométrie et dessin des blocs, partagés par la palette et la zone de programme
namespace blockview {

inline constexpr qreal Dome = 18;     // arrondi au-dessus d'un chapeau
inline constexpr qreal RowH = 40;     // hauteur d'un bloc (ou de la tête d'une boucle)
inline constexpr qreal ArmW = 16;     // bras gauche d'une boucle
inline constexpr qreal MinInner = 24; // corps de boucle vide
inline constexpr qreal BottomH = 24;  // bas d'une boucle

inline const QString MimeType = QStringLiteral("application/x-batmobile-blocks");

// Bloc placé : position, contour et zones de texte
struct Item {
    Block *block = nullptr;
    BlockPtr *slot = nullptr;  // pointeur qui possède ce bloc (pour le détacher)
    QPointF pos;               // coin haut gauche (haut de l'arrondi pour un chapeau)
    qreal width = 0;
    qreal height = 0;          // le bloc suivant commence à pos.y + height
    QPainterPath path;
    QRectF before, after;      // texte avant / après le paramètre
    QRectF value;              // pastille du paramètre (vide s'il n'y en a pas)
};

QFont font();

// Place la pile qui commence à first (blocs suivants et corps des boucles compris),
// ajoute les blocs à out dans l'ordre de dessin et renvoie la hauteur de la pile.
qreal layout(BlockPtr &first, QPointF pos, QList<Item> &out);
QRectF bounds(const QList<Item> &items);
void paint(QPainter &p, const Item &item, bool highlight = false);

// Glisser-déposer d'une pile : hot = point saisi, relatif au coin haut gauche de la pile
Qt::DropAction drag(QWidget *source, BlockPtr &stack, const QPointF &hot);
BlockPtr fromMime(const QMimeData *mime, QPointF *hot);

}  // namespace blockview

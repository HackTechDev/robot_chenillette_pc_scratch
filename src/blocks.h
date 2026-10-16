#pragma once

#include <QJsonArray>
#include <QList>
#include <QRgb>
#include <QString>

#include <memory>

// Forme d'un bloc, comme dans Scratch
enum class Shape {
    Hat,    // chapeau : début de script, rien au-dessus
    Stack,  // bloc d'instruction
    C,      // boucle avec corps
    CapC,   // boucle sans fin : rien en dessous
};

enum class Param { None, Seconds, Count, Speed };

// L'ordre doit suivre la table Specs de blocks.cpp
enum class Kind {
    Start,
    Forward, Backward, Left, Right, Speed, Stop,
    ClawClose, ClawOpen, HeadUp, HeadDown,
    Wait, Repeat, Forever,
};

struct Spec {
    Kind kind;
    const char *id;    // nom dans les fichiers .batmobile
    const char *text;  // %1 = emplacement du paramètre
    QRgb color;
    Shape shape;
    Param param;
    char cmd;          // commande maintenue pendant la durée (0 = aucune)
    double def;        // valeur par défaut du paramètre
};

const Spec &specOf(Kind kind);
inline bool hasBody(Shape s) { return s == Shape::C || s == Shape::CapC; }

struct Category {
    QString name;
    QRgb color;
    QList<Kind> kinds;
};
const QList<Category> &categories();

// Bloc d'un script : liste chaînée (next), plus un corps pour les boucles
struct Block {
    explicit Block(Kind k);

    const Spec &spec() const { return specOf(kind); }

    Kind kind;
    double value;
    std::unique_ptr<Block> next;
    std::unique_ptr<Block> body;
};
using BlockPtr = std::unique_ptr<Block>;

Block *lastBlock(Block *b);

// Valeurs : arrondies et bornées selon le paramètre ; virgule ou point acceptés
double clampValue(Param p, double v);
QString formatValue(Param p, double v);
bool parseValue(Param p, QString text, double *out);
QString valuePattern(Param p);  // expression régulière de saisie

// Pile de blocs (le bloc et ses suivants) ↔ JSON ; les blocs inconnus sont ignorés
QJsonArray blocksToJson(const Block *first);
BlockPtr blocksFromJson(const QJsonArray &array, bool top = true);

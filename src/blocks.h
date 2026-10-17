#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRgb>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>

// Forme d'un bloc, comme dans Scratch
enum class Shape {
    Hat,       // chapeau : début de script, rien au-dessus
    Stack,     // bloc d'instruction
    C,         // boucle avec corps
    CapC,      // boucle sans fin : rien en dessous
    Reporter,  // valeur (arrondi) : se place dans une entrée d'un autre bloc
    Boolean,   // vrai / faux (hexagone) : se place dans une entrée de condition
    E,         // si … alors / sinon : deux corps
};

// Type des entrées d'un bloc (toutes ses entrées ont le même) ; Bool = condition
enum class Param { None, Seconds, Count, Speed, Key, Number, Distance, Angle, Bool };

// L'ordre doit suivre la table Specs de blocks.cpp
enum class Kind {
    Start, KeyHat,
    Forward, Backward, Left, Right,
    GoForward, GoBackward, GoLeft, GoRight,  // sans durée : jusqu'au prochain stop
    ForwardCm, BackwardCm, LeftDeg, RightDeg,  // en cm / degrés : durée déduite du calibrage
    Speed, Stop,
    ClawClose, ClawOpen, HeadUp, HeadDown,
    Wait, Repeat, Forever,
    SetVar, ChangeVar, Variable,
    Add, Subtract, Multiply, Divide, Random,
    Define, Call,  // mes blocs : « définir nom » (chapeau) et l'appel « nom »
    If, IfElse, RepeatUntil,
    Less, Equal, Greater, And, Or, Not,
    ParamValue,  // mes blocs : valeur d'un paramètre, dans la définition
    Say, SayFor,  // bulle au-dessus du robot dans la scène
};

struct Spec {
    Kind kind;
    const char *id;    // nom dans les fichiers .batmobile
    // %1, %2 = entrées ; %v = liste des variables ; %n = nom (variable, bloc personnalisé) en texte ;
    // %t = texte libre (Block::text) ou n'importe quel reporter (dans inputs[TextInput])
    const char *text;
    QRgb color;
    Shape shape;
    Param param;
    char cmd;          // commande maintenue pendant la durée, ou jusqu'au stop si Param::None (0 = aucune)
    double def;        // valeurs par défaut des entrées
    double def2;
};

const Spec &specOf(Kind kind);
inline bool hasBody(Shape s) { return s == Shape::C || s == Shape::CapC || s == Shape::E; }
inline bool hasElse(Shape s) { return s == Shape::E; }
inline bool isReporter(Shape s) { return s == Shape::Reporter || s == Shape::Boolean; }
int inputCount(const Spec &s);
inline constexpr int MaxParams = 2;  // paramètres d'un bloc personnalisé (= entrées d'un bloc)
inline constexpr int TextInput = 1;  // entrée qui reçoit le reporter d'un texte %t
bool hasText(const Spec &s);
// Une entrée de ce type accepte ce reporter : nombre ← arrondi, condition ← hexagone (pas la touche)
inline bool acceptsShape(Param p, Shape s)
{
    if (p == Param::Bool)
        return s == Shape::Boolean;
    return s == Shape::Reporter && p != Param::None && p != Param::Key;
}

struct Category {
    QString name;
    QRgb color;
    QList<Kind> kinds;
};
const QList<Category> &categories();
QRgb variablesColor();

struct Block;
using BlockPtr = std::unique_ptr<Block>;

// Entrée : un nombre, ou un reporter qui le remplace
struct Input {
    double value = 0;
    BlockPtr reporter;
};

// Bloc d'un script : liste chaînée (next), corps des boucles, entrées
struct Block {
    explicit Block(Kind k);

    const Spec &spec() const { return specOf(kind); }

    Kind kind;
    std::array<Input, 2> inputs;
    QString name;        // variable, bloc personnalisé ou paramètre
    QStringList params;  // mes blocs (définir / appel) : noms des paramètres
    QString text;        // texte libre (%t)
    BlockPtr next;
    BlockPtr body;
    BlockPtr body2;  // « sinon »
};

Block *lastBlock(Block *b);
int inputCount(const Block *b);  // un appel de mes blocs a une entrée par paramètre

// Bloc personnalisé défini dans le programme
struct Procedure {
    QString name;
    QStringList params;
    bool operator==(const Procedure &) const = default;
};

// Valeurs : arrondies et bornées selon le paramètre ; virgule ou point acceptés
double clampValue(Param p, double v);
QString formatValue(Param p, double v);
QString formatNumber(double v);  // valeur calculée (variable, opérateur)
bool parseValue(Param p, QString text, double *out);
QString valuePattern(Param p);  // expression régulière de saisie

// Touches du bloc « quand la touche … est pressée » : valeur = indice dans cette liste
int keyCount();
QString keyName(int index);
int keyIndex(int qtKey);  // -1 si la touche n'est pas proposée

// Pile de blocs (le bloc et ses suivants) ↔ JSON ; les blocs inconnus sont ignorés
QJsonArray blocksToJson(const Block *first);
BlockPtr blocksFromJson(const QJsonArray &array, bool top = true);

// Noms de variables utilisés par une pile (blocs suivants, corps et entrées compris)
void collectVariables(const Block *first, QStringList *names);

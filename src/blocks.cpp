#include "blocks.h"

#include "config.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// Couleurs des catégories Scratch
constexpr QRgb Events = 0xFFBF00;
constexpr QRgb Motion = 0x4C97FF;
constexpr QRgb Tools = 0x9966FF;
constexpr QRgb Control = 0xFFAB19;
constexpr QRgb Variables = 0xFF8C1A;
constexpr QRgb Operators = 0x59C059;
constexpr QRgb MyBlocks = 0xFF6680;
constexpr QRgb Looks = 0xCF63CF;

const Spec Specs[] = {
    {Kind::Start, "start", "quand ▶ est cliqué", Events, Shape::Hat, Param::None, 0, 0, 0},
    {Kind::KeyHat, "key", "quand la touche %1 est pressée", Events, Shape::Hat, Param::Key, 0, 0, 0},
    {Kind::Forward, "forward", "avancer pendant %1 s", Motion, Shape::Stack, Param::Seconds, 'z', 1, 0},
    {Kind::Backward, "backward", "reculer pendant %1 s", Motion, Shape::Stack, Param::Seconds, 's', 1, 0},
    {Kind::Left, "left", "pivoter ↺ pendant %1 s", Motion, Shape::Stack, Param::Seconds, 'q', 0.5, 0},
    {Kind::Right, "right", "pivoter ↻ pendant %1 s", Motion, Shape::Stack, Param::Seconds, 'd', 0.5, 0},
    {Kind::GoForward, "go_forward", "démarrer avancer", Motion, Shape::Stack, Param::None, 'z', 0, 0},
    {Kind::GoBackward, "go_backward", "démarrer reculer", Motion, Shape::Stack, Param::None, 's', 0, 0},
    {Kind::GoLeft, "go_left", "démarrer pivoter ↺", Motion, Shape::Stack, Param::None, 'q', 0, 0},
    {Kind::GoRight, "go_right", "démarrer pivoter ↻", Motion, Shape::Stack, Param::None, 'd', 0, 0},
    {Kind::ForwardCm, "forward_cm", "avancer de %1 cm", Motion, Shape::Stack, Param::Distance, 'z', 20, 0},
    {Kind::BackwardCm, "backward_cm", "reculer de %1 cm", Motion, Shape::Stack, Param::Distance, 's', 20, 0},
    {Kind::LeftDeg, "left_deg", "pivoter ↺ de %1 °", Motion, Shape::Stack, Param::Angle, 'q', 90, 0},
    {Kind::RightDeg, "right_deg", "pivoter ↻ de %1 °", Motion, Shape::Stack, Param::Angle, 'd', 90, 0},
    {Kind::Speed, "speed", "vitesse %1", Motion, Shape::Stack, Param::Speed, 0, SpeedLevelDefault, 0},
    {Kind::Stop, "stop", "stop", Motion, Shape::Stack, Param::None, 0, 0, 0},
    {Kind::ClawClose, "claw_close", "fermer la pince pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'v', 1, 0},
    {Kind::ClawOpen, "claw_open", "ouvrir la pince pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'c', 1, 0},
    {Kind::HeadUp, "head_up", "lever la tête pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'w', 0.5, 0},
    {Kind::HeadDown, "head_down", "baisser la tête pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'x', 0.5, 0},
    {Kind::Wait, "wait", "attendre %1 s", Control, Shape::Stack, Param::Seconds, 0, 1, 0},
    {Kind::Repeat, "repeat", "répéter %1 fois", Control, Shape::C, Param::Count, 0, 4, 0},
    {Kind::Forever, "forever", "répéter indéfiniment", Control, Shape::CapC, Param::None, 0, 0, 0},
    {Kind::SetVar, "set_var", "mettre %v à %1", Variables, Shape::Stack, Param::Number, 0, 0, 0},
    {Kind::ChangeVar, "change_var", "ajouter %1 à %v", Variables, Shape::Stack, Param::Number, 0, 1, 0},
    {Kind::Variable, "var", "%n", Variables, Shape::Reporter, Param::None, 0, 0, 0},
    {Kind::Add, "add", "%1 + %2", Operators, Shape::Reporter, Param::Number, 0, 0, 0},
    {Kind::Subtract, "subtract", "%1 − %2", Operators, Shape::Reporter, Param::Number, 0, 0, 0},
    {Kind::Multiply, "multiply", "%1 × %2", Operators, Shape::Reporter, Param::Number, 0, 1, 1},
    {Kind::Divide, "divide", "%1 ÷ %2", Operators, Shape::Reporter, Param::Number, 0, 1, 1},
    {Kind::Random, "random", "nombre aléatoire entre %1 et %2", Operators, Shape::Reporter, Param::Number, 0, 1, 10},
    {Kind::Define, "define", "définir %n", MyBlocks, Shape::Hat, Param::None, 0, 0, 0},
    {Kind::Call, "call", "%n", MyBlocks, Shape::Stack, Param::Number, 0, 0, 0},  // entrées : inputCount(Block*)
    {Kind::If, "if", "si %1 alors", Control, Shape::C, Param::Bool, 0, 0, 0},
    {Kind::IfElse, "if_else", "si %1 alors", Control, Shape::E, Param::Bool, 0, 0, 0},
    {Kind::RepeatUntil, "repeat_until", "répéter jusqu'à %1", Control, Shape::C, Param::Bool, 0, 0, 0},
    {Kind::Less, "less", "%1 < %2", Operators, Shape::Boolean, Param::Number, 0, 0, 50},
    {Kind::Equal, "equal", "%1 = %2", Operators, Shape::Boolean, Param::Number, 0, 0, 50},
    {Kind::Greater, "greater", "%1 > %2", Operators, Shape::Boolean, Param::Number, 0, 0, 50},
    {Kind::And, "and", "%1 et %2", Operators, Shape::Boolean, Param::Bool, 0, 0, 0},
    {Kind::Or, "or", "%1 ou %2", Operators, Shape::Boolean, Param::Bool, 0, 0, 0},
    {Kind::Not, "not", "non %1", Operators, Shape::Boolean, Param::Bool, 0, 0, 0},
    {Kind::ParamValue, "param", "%n", MyBlocks, Shape::Reporter, Param::None, 0, 0, 0},
    {Kind::Say, "say", "dire %t", Looks, Shape::Stack, Param::None, 0, 0, 0},
    {Kind::SayFor, "say_for", "dire %t pendant %1 s", Looks, Shape::Stack, Param::Seconds, 0, 2, 0},
};
static_assert(std::size(Specs) == size_t(Kind::SayFor) + 1);

const QString DefaultText = QStringLiteral("Bonjour !");

// Espace, flèches, lettres, chiffres (Qt::Key ne dépend pas de la disposition du clavier)
struct KeyDef {
    QString name;
    int key;
};

const QList<KeyDef> &keys()
{
    static const QList<KeyDef> list = [] {
        QList<KeyDef> l = {
            {QStringLiteral("espace"), Qt::Key_Space},
            {QStringLiteral("↑"), Qt::Key_Up},
            {QStringLiteral("↓"), Qt::Key_Down},
            {QStringLiteral("←"), Qt::Key_Left},
            {QStringLiteral("→"), Qt::Key_Right},
        };
        for (char c = 'a'; c <= 'z'; ++c)
            l.append({QString(QChar(c)), Qt::Key_A + (c - 'a')});
        for (char c = '0'; c <= '9'; ++c)
            l.append({QString(QChar(c)), Qt::Key_0 + (c - '0')});
        return l;
    }();
    return list;
}

const Spec *findSpec(const QString &id)
{
    for (const Spec &s : Specs) {
        if (id == QLatin1String(s.id))
            return &s;
    }
    return nullptr;
}

struct Range {
    double min, max;
    int decimals;
};

Range range(Param p)
{
    switch (p) {
    case Param::Seconds: return {0, 60, 2};  // 0,25 s = un huitième de tour
    case Param::Count: return {0, 100, 0};
    case Param::Speed: return {0, SpeedLevels - 1, 0};
    case Param::Key: return {0, double(keys().size() - 1), 0};
    case Param::Number: return {-9999, 9999, 2};
    case Param::Distance: return {0, 500, 1};
    case Param::Angle: return {0, 3600, 0};
    case Param::Bool:  // pas de valeur : vide = faux
    case Param::None: break;
    }
    return {0, 0, 0};
}

}  // namespace

const Spec &specOf(Kind kind)
{
    return Specs[int(kind)];
}

bool hasText(const Spec &s)
{
    return QString::fromUtf8(s.text).contains(QLatin1String("%t"));
}

int inputCount(const Spec &s)
{
    const QString text = QString::fromUtf8(s.text);
    return text.contains(QLatin1String("%2")) ? 2 : text.contains(QLatin1String("%1")) ? 1 : 0;
}

QRgb variablesColor()
{
    return Variables;
}

const QList<Category> &categories()
{
    static const QList<Category> list = {
        {QStringLiteral("Événements"), Events, {Kind::Start, Kind::KeyHat}},
        {QStringLiteral("Mouvement"), Motion,
         {Kind::ForwardCm, Kind::BackwardCm, Kind::LeftDeg, Kind::RightDeg, Kind::Forward, Kind::Backward,
          Kind::Left, Kind::Right, Kind::GoForward, Kind::GoBackward, Kind::GoLeft, Kind::GoRight, Kind::Stop,
          Kind::Speed}},
        {QStringLiteral("Pince et tête"), Tools, {Kind::ClawClose, Kind::ClawOpen, Kind::HeadUp, Kind::HeadDown}},
        {QStringLiteral("Apparence"), Looks, {Kind::Say, Kind::SayFor}},
        {QStringLiteral("Contrôle"), Control,
         {Kind::Wait, Kind::Repeat, Kind::Forever, Kind::If, Kind::IfElse, Kind::RepeatUntil}},
        {QStringLiteral("Opérateurs"), Operators,
         {Kind::Add, Kind::Subtract, Kind::Multiply, Kind::Divide, Kind::Random, Kind::Less, Kind::Equal,
          Kind::Greater, Kind::And, Kind::Or, Kind::Not}},
    };
    return list;
}

Block::Block(Kind k) : kind(k)
{
    inputs[0].value = specOf(k).def;
    inputs[1].value = specOf(k).def2;
    if (hasText(specOf(k)))
        text = DefaultText;
}

int inputCount(const Block *b)
{
    if (b->kind == Kind::Call)
        return qMin(int(b->params.size()), MaxParams);
    return inputCount(b->spec());
}

Block *lastBlock(Block *b)
{
    while (b && b->next)
        b = b->next.get();
    return b;
}

double clampValue(Param p, double v)
{
    const Range r = range(p);
    const double scale = std::pow(10.0, r.decimals);
    return std::clamp(std::round(v * scale) / scale, r.min, r.max);
}

QString formatValue(Param p, double v)
{
    if (p == Param::Key)
        return keyName(int(clampValue(p, v))) + QStringLiteral(" ▾");  // liste déroulante
    if (p == Param::Bool)
        return {};
    QString text = QString::number(clampValue(p, v), 'f', range(p).decimals);
    // Durées : une décimale au moins (« 1,0 »), deux si besoin (« 0,25 ») ; nombres : « 1 », « 0,5 »
    const qsizetype minDecimals = p == Param::Seconds ? 1 : 0;
    while (text.indexOf('.') >= 0 && text.size() - text.indexOf('.') - 1 > minDecimals
           && (text.endsWith('0') || text.endsWith('.')))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text.replace('.', ',');
}

QString formatNumber(double v)
{
    return QString::number(std::round(v * 100) / 100, 'g', 10).replace('.', ',');
}

bool parseValue(Param p, QString text, double *out)
{
    bool ok = false;
    const double v = text.replace(',', '.').trimmed().toDouble(&ok);
    if (ok)
        *out = clampValue(p, v);
    return ok;
}

QString valuePattern(Param p)
{
    switch (p) {
    case Param::Seconds: return QStringLiteral("\\d{0,2}([.,]\\d{0,2})?");
    case Param::Count: return QStringLiteral("\\d{0,3}");
    case Param::Speed: return QStringLiteral("\\d?");
    case Param::Number: return QStringLiteral("-?\\d{0,4}([.,]\\d{0,2})?");
    case Param::Distance: return QStringLiteral("\\d{0,3}([.,]\\d?)?");
    case Param::Angle: return QStringLiteral("\\d{0,4}");
    case Param::Key:  // choisie dans un menu
    case Param::Bool:
    case Param::None: break;
    }
    return {};
}

int keyCount()
{
    return keys().size();
}

QString keyName(int index)
{
    return keys().value(index).name;
}

int keyIndex(int qtKey)
{
    for (int i = 0; i < keys().size(); ++i) {
        if (keys()[i].key == qtKey)
            return i;
    }
    return -1;
}

namespace {

const QString InputKeys[2][2] = {
    {QStringLiteral("value"), QStringLiteral("arg")},
    {QStringLiteral("value2"), QStringLiteral("arg2")},
};

QJsonObject blockToJson(const Block *b)
{
    const Spec &s = b->spec();
    QJsonObject o{{QStringLiteral("kind"), QLatin1String(s.id)}};
    if (!b->params.isEmpty())
        o[QStringLiteral("params")] = QJsonArray::fromStringList(b->params);
    for (int i = 0; i < inputCount(b); ++i) {
        if (s.param != Param::Bool)
            o[InputKeys[i][0]] = b->inputs[i].value;
        if (b->inputs[i].reporter)
            o[InputKeys[i][1]] = blockToJson(b->inputs[i].reporter.get());
    }
    if (!b->name.isEmpty())
        o[QStringLiteral("name")] = b->name;
    if (hasText(s)) {
        o[QStringLiteral("text")] = b->text;
        if (const Block *r = b->inputs[TextInput].reporter.get())
            o[InputKeys[TextInput][1]] = blockToJson(r);
    }
    if (b->body)
        o[QStringLiteral("body")] = blocksToJson(b->body.get());
    if (b->body2)
        o[QStringLiteral("body2")] = blocksToJson(b->body2.get());
    return o;
}

// Un bloc seul (sans ses suivants) ; nullptr si inconnu
BlockPtr blockFromJson(const QJsonObject &o)
{
    const Spec *s = findSpec(o[QStringLiteral("kind")].toString());
    if (!s)
        return nullptr;
    auto b = std::make_unique<Block>(s->kind);
    for (const QJsonValue &p : o[QStringLiteral("params")].toArray()) {
        if (b->params.size() < MaxParams && !p.toString().isEmpty())
            b->params.append(p.toString());
    }
    const double defs[2] = {s->def, s->def2};
    for (int i = 0; i < inputCount(b.get()); ++i) {
        b->inputs[i].value = clampValue(s->param, o[InputKeys[i][0]].toDouble(defs[i]));
        BlockPtr r = blockFromJson(o[InputKeys[i][1]].toObject());
        if (r && acceptsShape(s->param, r->spec().shape))
            b->inputs[i].reporter = std::move(r);
    }
    b->name = o[QStringLiteral("name")].toString();
    if (hasText(*s)) {
        b->text = o[QStringLiteral("text")].toString(DefaultText);
        BlockPtr r = blockFromJson(o[InputKeys[TextInput][1]].toObject());
        if (r && isReporter(r->spec().shape))  // texte : n'importe quel reporter
            b->inputs[TextInput].reporter = std::move(r);
    }
    if (hasBody(s->shape))
        b->body = blocksFromJson(o[QStringLiteral("body")].toArray(), false);
    if (hasElse(s->shape))
        b->body2 = blocksFromJson(o[QStringLiteral("body2")].toArray(), false);
    return b;
}

}  // namespace

QJsonArray blocksToJson(const Block *first)
{
    QJsonArray out;
    for (const Block *b = first; b; b = b->next.get())
        out.append(blockToJson(b));
    return out;
}

BlockPtr blocksFromJson(const QJsonArray &array, bool top)
{
    BlockPtr first;
    BlockPtr *slot = &first;
    for (const QJsonValue &v : array) {
        BlockPtr b = blockFromJson(v.toObject());
        if (!b)
            continue;
        const Shape shape = b->spec().shape;
        // Chapeau seulement en tête de script ; un reporter seulement seul, comme script
        if ((shape == Shape::Hat || isReporter(shape)) && (!top || slot != &first))
            continue;
        *slot = std::move(b);
        if (shape == Shape::CapC || isReporter(shape))  // rien après
            break;
        slot = &(*slot)->next;
    }
    return first;
}

void collectVariables(const Block *first, QStringList *names)
{
    for (const Block *b = first; b; b = b->next.get()) {
        if (!b->name.isEmpty() && (b->kind == Kind::Variable || b->kind == Kind::SetVar || b->kind == Kind::ChangeVar)
            && !names->contains(b->name))
            names->append(b->name);
        for (const Input &in : b->inputs)
            collectVariables(in.reporter.get(), names);
        collectVariables(b->body.get(), names);
        collectVariables(b->body2.get(), names);
    }
}

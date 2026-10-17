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

const Spec Specs[] = {
    {Kind::Start, "start", "quand ▶ est cliqué", Events, Shape::Hat, Param::None, 0, 0},
    {Kind::KeyHat, "key", "quand la touche %1 est pressée", Events, Shape::Hat, Param::Key, 0, 0},
    {Kind::Forward, "forward", "avancer pendant %1 s", Motion, Shape::Stack, Param::Seconds, 'z', 1},
    {Kind::Backward, "backward", "reculer pendant %1 s", Motion, Shape::Stack, Param::Seconds, 's', 1},
    {Kind::Left, "left", "pivoter ↺ pendant %1 s", Motion, Shape::Stack, Param::Seconds, 'q', 0.5},
    {Kind::Right, "right", "pivoter ↻ pendant %1 s", Motion, Shape::Stack, Param::Seconds, 'd', 0.5},
    {Kind::GoForward, "go_forward", "démarrer avancer", Motion, Shape::Stack, Param::None, 'z', 0},
    {Kind::GoBackward, "go_backward", "démarrer reculer", Motion, Shape::Stack, Param::None, 's', 0},
    {Kind::GoLeft, "go_left", "démarrer pivoter ↺", Motion, Shape::Stack, Param::None, 'q', 0},
    {Kind::GoRight, "go_right", "démarrer pivoter ↻", Motion, Shape::Stack, Param::None, 'd', 0},
    {Kind::Speed, "speed", "vitesse %1", Motion, Shape::Stack, Param::Speed, 0, SpeedLevelDefault},
    {Kind::Stop, "stop", "stop", Motion, Shape::Stack, Param::None, 0, 0},
    {Kind::ClawClose, "claw_close", "fermer la pince pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'v', 1},
    {Kind::ClawOpen, "claw_open", "ouvrir la pince pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'c', 1},
    {Kind::HeadUp, "head_up", "lever la tête pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'w', 0.5},
    {Kind::HeadDown, "head_down", "baisser la tête pendant %1 s", Tools, Shape::Stack, Param::Seconds, 'x', 0.5},
    {Kind::Wait, "wait", "attendre %1 s", Control, Shape::Stack, Param::Seconds, 0, 1},
    {Kind::Repeat, "repeat", "répéter %1 fois", Control, Shape::C, Param::Count, 0, 4},
    {Kind::Forever, "forever", "répéter indéfiniment", Control, Shape::CapC, Param::None, 0, 0},
};
static_assert(std::size(Specs) == size_t(Kind::Forever) + 1);

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
    case Param::None: break;
    }
    return {0, 0, 0};
}

}  // namespace

const Spec &specOf(Kind kind)
{
    return Specs[int(kind)];
}

const QList<Category> &categories()
{
    static const QList<Category> list = {
        {QStringLiteral("Événements"), Events, {Kind::Start, Kind::KeyHat}},
        {QStringLiteral("Mouvement"), Motion,
         {Kind::Forward, Kind::Backward, Kind::Left, Kind::Right, Kind::GoForward, Kind::GoBackward, Kind::GoLeft,
          Kind::GoRight, Kind::Stop, Kind::Speed}},
        {QStringLiteral("Pince et tête"), Tools, {Kind::ClawClose, Kind::ClawOpen, Kind::HeadUp, Kind::HeadDown}},
        {QStringLiteral("Contrôle"), Control, {Kind::Wait, Kind::Repeat, Kind::Forever}},
    };
    return list;
}

Block::Block(Kind k) : kind(k), value(specOf(k).def) {}

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
    QString text = QString::number(clampValue(p, v), 'f', range(p).decimals);
    // Durées : une décimale au moins (« 1,0 »), deux si besoin (« 0,25 »)
    while (text.endsWith('0') && text.indexOf('.') >= 0 && text.size() - text.indexOf('.') > 2)
        text.chop(1);
    return text.replace('.', ',');
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
    case Param::Key:  // choisie dans un menu
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

QJsonArray blocksToJson(const Block *first)
{
    QJsonArray out;
    for (const Block *b = first; b; b = b->next.get()) {
        QJsonObject o{{QStringLiteral("kind"), QLatin1String(b->spec().id)}};
        if (b->spec().param != Param::None)
            o[QStringLiteral("value")] = b->value;
        if (b->body)
            o[QStringLiteral("body")] = blocksToJson(b->body.get());
        out.append(o);
    }
    return out;
}

BlockPtr blocksFromJson(const QJsonArray &array, bool top)
{
    BlockPtr first;
    BlockPtr *slot = &first;
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        const Spec *s = findSpec(o[QStringLiteral("kind")].toString());
        // Chapeau seulement en tête de script
        if (!s || (s->shape == Shape::Hat && (!top || slot != &first)))
            continue;
        auto b = std::make_unique<Block>(s->kind);
        if (s->param != Param::None)
            b->value = clampValue(s->param, o[QStringLiteral("value")].toDouble(s->def));
        if (hasBody(s->shape))
            b->body = blocksFromJson(o[QStringLiteral("body")].toArray(), false);
        *slot = std::move(b);
        if (s->shape == Shape::CapC)  // rien après une boucle sans fin
            break;
        slot = &(*slot)->next;
    }
    return first;
}

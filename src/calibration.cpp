#include "calibration.h"

#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <cmath>

namespace {

// Valeurs par défaut (non mesurées) : 20 cm/s et 180°/s à la vitesse 200 ; t0 = moitié de la
// rampe du firmware (+1 de vitesse toutes les 2 ms, soit 500 unités/s)
constexpr double CmPerSpeed = 0.1;
constexpr double DegreesPerSpeed = 0.9;
constexpr double RampPerS = 500;

const char *const MotionKeys[] = {"forward", "backward", "left", "right"};

double motorSpeed(int level)
{
    return (level + 1) * SpeedPerLevel;
}

}  // namespace

bool Calibration::motionOf(char cmd, Motion *m)
{
    switch (cmd) {
    case 'z': *m = Forward; return true;
    case 's': *m = Backward; return true;
    case 'q': *m = Left; return true;
    case 'd': *m = Right; return true;
    default: return false;
    }
}

QString Calibration::motionName(Motion m)
{
    switch (m) {
    case Forward: return QStringLiteral("avancer");
    case Backward: return QStringLiteral("reculer");
    case Left: return QStringLiteral("pivoter ↺");
    case Right: return QStringLiteral("pivoter ↻");
    }
    return {};
}

Calibration::Rate Calibration::defaultRate(Motion m, int level)
{
    const double v = motorSpeed(level);
    if (isTurn(m))
        return {v * DegreesPerSpeed, 0};
    return {v * CmPerSpeed, v / RampPerS / 2};
}

Calibration::Rate Calibration::rate(Motion m, int level) const
{
    const QMap<int, Rate> &points = measured_[m];
    if (points.isEmpty())
        return defaultRate(m, level);
    if (const auto it = points.constFind(level); it != points.cend())
        return *it;
    // Mesures de part et d'autre : interpolation linéaire
    auto above = points.lowerBound(level);
    if (above != points.cend() && above != points.cbegin()) {
        const auto below = std::prev(above);
        const double f = double(level - below.key()) / (above.key() - below.key());
        return {below->perS + f * (above->perS - below->perS), below->t0 + f * (above->t0 - below->t0)};
    }
    // D'un seul côté : proportionnel à la vitesse moteur (la rampe aussi)
    const auto nearest = above == points.cend() ? std::prev(points.cend()) : above;
    const double ratio = motorSpeed(level) / motorSpeed(nearest.key());
    return {nearest->perS * ratio, nearest->t0 * ratio};
}

double Calibration::duration(Motion m, int level, double amount) const
{
    const Rate r = rate(m, level);
    if (amount <= 0 || r.perS <= 0)
        return 0;
    if (isTurn(m) || r.t0 <= 0)
        return amount / r.perS + (isTurn(m) ? 0 : r.t0);
    // Rampe : perS × t0 parcourus en 2 t0 ; au-delà, vitesse établie
    if (amount >= r.perS * r.t0)
        return amount / r.perS + r.t0;
    return 2 * std::sqrt(r.t0 * amount / r.perS);
}

double Calibration::amount(Motion m, int level, double duration) const
{
    const Rate r = rate(m, level);
    if (duration <= 0)
        return 0;
    if (isTurn(m) || r.t0 <= 0)
        return r.perS * (isTurn(m) ? duration : qMax(0.0, duration - r.t0));
    if (duration >= 2 * r.t0)
        return r.perS * (duration - r.t0);
    return r.perS * duration * duration / (4 * r.t0);
}

bool Calibration::fromStraight(double d1, double d3, Rate *r, QString *error)
{
    // d = perS × (t − t0) pour t = 1 s et 3 s
    const double perS = (d3 - d1) / 2;
    if (d1 <= 0 || perS <= 0) {
        *error = QStringLiteral("La distance sur 3 s doit être plus grande que celle sur 1 s.");
        return false;
    }
    const double t0 = 1 - d1 / perS;
    if (t0 < 0 || t0 >= 0.5) {
        // t0 < 0 : le robot irait plus loin en 1 s qu'à vitesse établie ; ≥ 0,5 s : rampe trop longue
        *error = QStringLiteral("Mesures incohérentes (départ de %1 s) : refaire les deux essais.")
                     .arg(QString::number(t0, 'f', 2).replace('.', ','));
        return false;
    }
    *r = {perS, t0};
    return true;
}

QJsonObject Calibration::toJson() const
{
    QJsonObject o;
    for (int m = 0; m < MotionCount; ++m) {
        QJsonObject levels;
        for (auto it = measured_[m].cbegin(); it != measured_[m].cend(); ++it)
            levels[QString::number(it.key())] = QJsonObject{{QStringLiteral("perS"), it->perS}, {QStringLiteral("t0"), it->t0}};
        o[QLatin1String(MotionKeys[m])] = levels;
    }
    return {{QStringLiteral("format"), QStringLiteral("batmobile-calibration")}, {QStringLiteral("motions"), o}};
}

Calibration Calibration::fromJson(const QJsonObject &o)
{
    Calibration c;
    const QJsonObject motions = o[QStringLiteral("motions")].toObject();
    for (int m = 0; m < MotionCount; ++m) {
        const QJsonObject levels = motions[QLatin1String(MotionKeys[m])].toObject();
        for (auto it = levels.begin(); it != levels.end(); ++it) {
            bool ok = false;
            const int level = it.key().toInt(&ok);
            const QJsonObject r = it.value().toObject();
            const double perS = r[QStringLiteral("perS")].toDouble();
            if (ok && level >= 0 && level < SpeedLevels && perS > 0)
                c.measured_[m][level] = {perS, qMax(0.0, r[QStringLiteral("t0")].toDouble())};
        }
    }
    return c;
}

bool Calibration::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o[QStringLiteral("format")].toString() != QLatin1String("batmobile-calibration"))
        return false;
    *this = fromJson(o);
    return true;
}

bool Calibration::save(const QString &path) const
{
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(toJson()).toJson());
    return file.commit();
}

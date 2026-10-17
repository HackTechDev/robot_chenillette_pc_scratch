#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>

#include <array>

// Calibrage du robot : relation entre la durée d'une commande et la distance (cm) ou
// l'angle (degrés) parcouru, pour chaque niveau de vitesse.
//
// Ligne droite : le firmware accélère progressivement (rampe), l'arrêt est immédiat ;
// distance ≈ perS × (durée − t0) une fois la rampe passée (t0 ≈ moitié de la rampe,
// délai Bluetooth compris). Pendant la rampe (durée < 2 t0) : perS × durée² / (4 t0).
// Pivots : pas de rampe, angle = perS × durée.
//
// Seuls quelques niveaux sont mesurés ; les autres sont interpolés entre deux mesures,
// ou extrapolés proportionnellement à la vitesse moteur.
class Calibration
{
public:
    enum Motion { Forward, Backward, Left, Right };
    static constexpr int MotionCount = 4;

    struct Rate {
        double perS = 0;  // cm/s (ligne droite) ou degrés/s (pivot) à vitesse établie
        double t0 = 0;    // s perdues au démarrage (ligne droite seulement)
    };

    static bool isTurn(Motion m) { return m == Left || m == Right; }
    static bool motionOf(char cmd, Motion *m);  // 'z' 's' 'q' 'd'
    static QString motionName(Motion m);

    Rate rate(Motion m, int level) const;
    bool isMeasured(Motion m, int level) const { return measured_[m].contains(level); }
    QList<int> measuredLevels(Motion m) const { return measured_[m].keys(); }
    void setMeasured(Motion m, int level, Rate r) { measured_[m][level] = r; }
    void clear(Motion m) { measured_[m].clear(); }

    // Durée (s) d'une commande pour parcourir amount (cm ou degrés) au niveau de vitesse donné
    double duration(Motion m, int level, double amount) const;
    // Distance ou angle parcouru en duration secondes (inverse de duration)
    double amount(Motion m, int level, double duration) const;

    // Ligne droite mesurée sur 1 s et 3 s → Rate ; false si les mesures sont incohérentes
    static bool fromStraight(double d1, double d3, Rate *r, QString *error);

    QJsonObject toJson() const;
    static Calibration fromJson(const QJsonObject &o);
    bool load(const QString &path);
    bool save(const QString &path) const;

private:
    static Rate defaultRate(Motion m, int level);

    std::array<QMap<int, Rate>, MotionCount> measured_;
};

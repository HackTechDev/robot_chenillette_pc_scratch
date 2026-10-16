// Appairage automatique du robot BatMobile (BlueZ via D-Bus).
//
// Allume l'adaptateur, cherche l'appareil nommé « BatMobile », l'appaire avec le PIN 0000
// et le marque de confiance. Ne fait rien s'il est déjà appairé (sauf --repair).
// Affiche « ADDRESS=xx:xx:xx:xx:xx:xx » en dernière ligne en cas de succès.
//
// Usage : bt_pairing [--repair] [--timeout 30]

#include "config.h"
#include "pairing.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Appaire automatiquement le robot %1 (PIN %2).").arg(RobotName, RobotPin));
    parser.addHelpOption();
    const QCommandLineOption repair(QStringLiteral("repair"),
                                    QStringLiteral("supprimer l'appairage existant et recommencer"));
    const QCommandLineOption timeout(QStringLiteral("timeout"),
                                     QStringLiteral("durée maximale de recherche en secondes (défaut 30)"),
                                     QStringLiteral("secondes"), QStringLiteral("30"));
    parser.addOptions({repair, timeout});
    parser.process(app);

    bool ok = false;
    const int timeoutSec = parser.value(timeout).toInt(&ok);
    if (!ok || timeoutSec <= 0) {
        err << "Erreur : --timeout attend un nombre de secondes positif" << Qt::endl;
        return 2;
    }

    Pairing pairing(parser.isSet(repair), timeoutSec);
    QObject::connect(&pairing, &Pairing::progress, [&](const QString &msg) { out << msg << Qt::endl; });
    QObject::connect(&pairing, &Pairing::succeeded, [&](const QString &address) {
        out << "ADDRESS=" << address << Qt::endl;
        app.exit(0);
    });
    QObject::connect(&pairing, &Pairing::failed, [&](const QString &error) {
        err << "Erreur : " << error << Qt::endl;
        app.exit(1);
    });
    QTimer::singleShot(0, &pairing, &Pairing::start);
    return app.exec();
}

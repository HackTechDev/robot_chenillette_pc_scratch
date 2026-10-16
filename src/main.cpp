// BatMobile Scratch — programmation par blocs (façon Scratch) du robot Zumo, en
// Bluetooth classique (SPP/RFCOMM).
//
// Même protocole que la télécommande (../robot_chenillette_pc_qt6) : un bloc « pendant N s »
// envoie sa commande, attend N secondes, puis envoie un espace (stop).

#include "window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    Window w;
    w.resize(1100, 720);
    w.show();
    return app.exec();
}

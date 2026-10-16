#include "runner.h"

#include "config.h"
#include "link.h"

namespace {

// Durée minimale d'une attente, et pause d'un tour de boucle sans attente
constexpr int MinWaitMs = 20;

}  // namespace

Runner::Runner(Link &link, QObject *parent) : QObject(parent), link_(link)
{
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &Runner::onTimeout);
}

void Runner::start(const Block *first)
{
    stop();
    running_ = true;
    frames_ = {{first, nullptr, 0, waits_}};
    link_.send(char('0' + SpeedLevelDefault));  // chaque exécution part de la vitesse par défaut
    step();
}

void Runner::stop()
{
    if (!running_)
        return;
    timer_.stop();
    frames_.clear();
    moving_ = false;
    link_.send(Stop);
    finish();
}

void Runner::finish()
{
    running_ = false;
    emit current(nullptr);
    emit finished();
}

void Runner::onTimeout()
{
    if (moving_) {
        moving_ = false;
        link_.send(Stop);
    }
    step();
}

void Runner::wait(double seconds)
{
    ++waits_;
    timer_.start(qMax(MinWaitMs, qRound(seconds * 1000)));
}

void Runner::step()
{
    while (!frames_.isEmpty()) {
        Frame &f = frames_.last();
        if (!f.next) {
            // Fin de la séquence : tour de boucle suivant, ou retour au niveau au-dessus
            if (!f.loop || (f.loop->kind != Kind::Forever && --f.remaining <= 0)) {
                frames_.removeLast();
                continue;
            }
            f.next = f.loop->body.get();
            const bool idle = f.waits == waits_;
            if (idle)
                wait(0);  // tour sans attente (corps vide ou instantané) : on laisse respirer l'interface
            f.waits = waits_;
            if (idle)
                return;
            continue;
        }

        const Block *b = f.next;
        f.next = b->next.get();
        emit current(b);
        switch (b->kind) {
        case Kind::Start:
            break;
        case Kind::Repeat:
            if (b->value >= 1)
                frames_.append({b->body.get(), b, int(b->value), waits_});  // f n'est plus valide
            break;
        case Kind::Forever:
            frames_.append({b->body.get(), b, 0, waits_});
            break;
        case Kind::Speed:
            link_.send(char('0' + int(b->value)));
            break;
        case Kind::Stop:
            link_.send(Stop);
            break;
        case Kind::Wait:
            return wait(b->value);
        default:
            link_.send(b->spec().cmd);
            moving_ = true;
            return wait(b->value);
        }
    }
    finish();
}

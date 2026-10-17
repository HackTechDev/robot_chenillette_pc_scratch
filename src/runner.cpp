#include "runner.h"

#include "config.h"
#include "link.h"

#include <QRandomGenerator>

#include <cmath>

namespace {

// Durée minimale d'une attente, et pause d'un tour de boucle sans attente
constexpr int MinWaitMs = 20;

}  // namespace

Runner::Runner(Link &link, QObject *parent) : QObject(parent), link_(link)
{
    timer_.setSingleShot(true);
    timer_.setTimerType(Qt::PreciseTimer);  // un minuteur « coarse » peut décaler une durée de 5 %
    connect(&timer_, &QTimer::timeout, this, &Runner::onTimeout);
}

void Runner::start(const Block *first, bool stepByStep, bool resetSpeed)
{
    stop();
    running_ = true;
    stepByStep_ = stepByStep;
    acted_ = true;  // pas à pas : arrêt avant le premier bloc d'action
    frames_ = {{first, nullptr, 0, waits_}};
    if (resetSpeed)
        send(char('0' + SpeedLevelDefault));  // chaque exécution part de la vitesse par défaut
    step();
}

void Runner::resume(bool stepByStep)
{
    if (!paused_)
        return;
    paused_ = false;
    stepByStep_ = stepByStep;
    acted_ = false;
    step();
}

void Runner::stop()
{
    if (!running_)
        return;
    paused_ = false;
    timer_.stop();
    frames_.clear();
    moving_ = false;
    send(Stop);
    finish();
}

void Runner::resetVariables()
{
    vars_.clear();
}

double Runner::evaluate(const Block *r)
{
    auto arg = [&](int i) { return input(r, i); };
    switch (r->kind) {
    case Kind::Variable: return vars_.value(r->name);
    case Kind::Add: return arg(0) + arg(1);
    case Kind::Subtract: return arg(0) - arg(1);
    case Kind::Multiply: return arg(0) * arg(1);
    case Kind::Divide: {
        const double d = arg(1);
        return d == 0 ? 0 : arg(0) / d;
    }
    case Kind::Random: {
        // Entiers si les deux bornes sont entières, comme dans Scratch
        const double a = arg(0), b = arg(1);
        const double lo = std::min(a, b), hi = std::max(a, b);
        if (lo == std::floor(lo) && hi == std::floor(hi))
            return std::floor(lo + QRandomGenerator::global()->bounded(hi - lo + 1));
        return lo + QRandomGenerator::global()->bounded(hi - lo);
    }
    default: return 0;
    }
}

double Runner::input(const Block *b, int i)
{
    const Input &in = b->inputs[i];
    return in.reporter ? evaluate(in.reporter.get()) : in.value;
}

void Runner::setVariable(const QString &name, double value)
{
    if (name.isEmpty())
        return;
    value = std::round(value * 100) / 100;  // pas de 0,30000000000000004
    vars_[name] = value;
    emit variableChanged(name, value);
}

void Runner::send(char cmd)
{
    link_.send(cmd);  // sans effet si le robot n'est pas connecté
    emit sent(cmd);
    if (cmd == Stop)
        rolling_ = false;
}

void Runner::stopRolling()
{
    if (rolling_)
        send(Stop);
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
        send(Stop);
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
        const bool action = b->spec().shape == Shape::Stack;
        if (action && stepByStep_) {
            if (acted_) {
                stopRolling();  // en pause, le robot ne doit pas continuer à rouler
                paused_ = true;
                emit current(b);
                emit paused(b);
                return;
            }
            acted_ = true;
        }
        f.next = b->next.get();
        emit current(b);
        switch (b->kind) {
        case Kind::Start:
        case Kind::KeyHat:
            break;
        case Kind::Repeat:
            if (const int n = int(input(b, 0, Param::Count)); n >= 1)
                frames_.append({b->body.get(), b, n, waits_});  // f n'est plus valide
            break;
        case Kind::Forever:
            frames_.append({b->body.get(), b, 0, waits_});
            break;
        case Kind::Speed:
            send(char('0' + int(input(b, 0, Param::Speed))));
            break;
        case Kind::Stop:
            send(Stop);
            break;
        case Kind::Wait:
            return wait(input(b, 0, Param::Seconds));
        case Kind::SetVar:
            setVariable(b->name, input(b, 0));
            break;
        case Kind::ChangeVar:
            setVariable(b->name, vars_.value(b->name) + input(b, 0));
            break;
        case Kind::GoForward:
        case Kind::GoBackward:
        case Kind::GoLeft:
        case Kind::GoRight:
            send(b->spec().cmd);
            rolling_ = true;
            break;
        default:
            if (!b->spec().cmd)
                break;  // reporter seul : rien à envoyer
            send(b->spec().cmd);
            moving_ = true;
            return wait(input(b, 0, Param::Seconds));
        }
    }
    stopRolling();  // fin du programme : le robot s'arrête
    finish();
}

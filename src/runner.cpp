#include "runner.h"

#include "config.h"
#include "link.h"

#include <QRandomGenerator>

#include <cmath>

namespace {

// Durée minimale d'une attente, et pause d'un tour de boucle sans attente
constexpr int MinWaitMs = 20;
// Profondeur maximale (boucles et appels de mes blocs imbriqués) : arrête une récursion sans fin
constexpr int MaxDepth = 1000;

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
    emit said(QString());  // bulle de l'exécution précédente
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
    saying_ = false;
    emit said(QString());
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
    case Kind::ParamValue: return argument(r->name);
    case Kind::Add: return arg(0) + arg(1);
    case Kind::Subtract: return arg(0) - arg(1);
    case Kind::Multiply: return arg(0) * arg(1);
    case Kind::Divide: {
        const double d = arg(1);
        return d == 0 ? 0 : arg(0) / d;
    }
    case Kind::Less: return arg(0) < arg(1);
    case Kind::Equal: return std::abs(arg(0) - arg(1)) < 1e-9;
    case Kind::Greater: return arg(0) > arg(1);
    case Kind::And: return condition(r, 0) && condition(r, 1);
    case Kind::Or: return condition(r, 0) || condition(r, 1);
    case Kind::Not: return !condition(r, 0);
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

double Runner::argument(const QString &name) const
{
    // Appel le plus proche : les boucles et « si » de la définition sont au-dessus de lui
    for (qsizetype i = frames_.size() - 1; i >= 0; --i) {
        if (frames_[i].call)
            return frames_[i].args.value(name);
    }
    return 0;  // hors d'un appel (clic sur le reporter seul)
}

QString Runner::sayText(const Block *b)
{
    const Block *r = b->inputs[TextInput].reporter.get();
    if (!r)
        return b->text;
    const double v = evaluate(r);
    if (r->spec().shape == Shape::Boolean)
        return v != 0 ? QStringLiteral("vrai") : QStringLiteral("faux");
    return formatNumber(v);
}

bool Runner::condition(const Block *b, int i)
{
    const Block *r = b->inputs[i].reporter.get();
    return r && evaluate(r) != 0;
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
    else if (cmd >= '0' && cmd <= '9')
        speedLevel_ = cmd - '0';
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
    if (saying_) {
        saying_ = false;
        emit said(QString());
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
            // Fin du corps : tour suivant, ou retour au niveau au-dessus
            bool again = false;
            if (f.loop) {
                switch (f.loop->kind) {
                case Kind::Forever: again = true; break;
                case Kind::RepeatUntil: again = !condition(f.loop, 0); break;
                default: again = --f.remaining > 0; break;
                }
            }
            if (!again) {
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
        const bool action = b->spec().shape == Shape::Stack && b->kind != Kind::Call;
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
        case Kind::Define:
            break;
        case Kind::Call:
            if (const Block *def = procedures_ ? procedures_(b->name) : nullptr) {
                if (frames_.size() >= MaxDepth) {
                    stop();
                    emit error(QStringLiteral("Programme arrêté : le bloc « %1 » s'appelle sans fin.").arg(b->name));
                    return;
                }
                // Valeurs calculées dans le contexte de l'appelant, avant d'entrer dans la définition
                QHash<QString, double> args;
                for (int i = 0; i < inputCount(b); ++i)
                    args[b->params[i]] = input(b, i);
                frames_.append({def->next.get(), nullptr, 0, waits_, args, true});  // f n'est plus valide
            }
            break;
        case Kind::Repeat:
            if (const int n = int(input(b, 0, Param::Count)); n >= 1)
                frames_.append({b->body.get(), b, n, waits_});  // f n'est plus valide
            break;
        case Kind::Forever:
            frames_.append({b->body.get(), b, 0, waits_});
            break;
        case Kind::RepeatUntil:
            if (!condition(b, 0))
                frames_.append({b->body.get(), b, 0, waits_});
            break;
        case Kind::If:
        case Kind::IfElse: {
            // Corps choisi : séquence sans boucle, comme un script (dépilée à la fin)
            const Block *body = condition(b, 0) ? b->body.get() : b->body2.get();
            if (body && frames_.size() < MaxDepth)
                frames_.append({body, nullptr, 0, waits_});
            break;
        }
        case Kind::Speed:
            send(char('0' + int(input(b, 0, Param::Speed))));
            break;
        case Kind::Stop:
            send(Stop);
            break;
        case Kind::Wait:
            return wait(input(b, 0, Param::Seconds));
        case Kind::Say:
            emit said(sayText(b));
            break;
        case Kind::SayFor:
            emit said(sayText(b));
            saying_ = true;
            return wait(input(b, 0, Param::Seconds));
        case Kind::SetVar:
            setVariable(b->name, input(b, 0));
            break;
        case Kind::ChangeVar:
            setVariable(b->name, vars_.value(b->name) + input(b, 0));
            break;
        case Kind::ForwardCm:
        case Kind::BackwardCm:
        case Kind::LeftDeg:
        case Kind::RightDeg: {
            const char cmd = b->spec().cmd;
            Calibration::Motion m;
            Calibration::motionOf(cmd, &m);
            const double seconds = calibration_.duration(m, speedLevel_, input(b, 0, b->spec().param));
            if (seconds <= 0)
                break;  // 0 cm : rien à faire
            send(cmd);
            moving_ = true;
            return wait(seconds);
        }
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

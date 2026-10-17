#include "calibrationdialog.h"

#include "config.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QString number(double v, int decimals)
{
    return QString::number(v, 'f', decimals).replace('.', ',');
}

QLabel *wrapped(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

QDoubleSpinBox *spin(double min, double max, double step, int decimals, const QString &suffix, double value)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setValue(value);
    return s;
}

}  // namespace

CalibrationDialog::CalibrationDialog(const Calibration &calibration, QWidget *parent)
    : QDialog(parent), calibration_(calibration)
{
    setWindowTitle(QStringLiteral("Calibrage du robot"));

    auto *intro = wrapped(QStringLiteral(
        "Le robot roule pendant une durée donnée ; on mesure ce qu'il a parcouru. Les blocs « avancer de N cm » "
        "et « pivoter de N ° » et la scène utilisent ensuite ces mesures. Mesurer sur le sol habituel, piles "
        "chargées ; quelques vitesses suffisent (les autres sont déduites)."));

    motion_ = new QComboBox;
    for (int m = 0; m < Calibration::MotionCount; ++m)
        motion_->addItem(Calibration::motionName(Calibration::Motion(m)));
    level_ = new QSpinBox;
    level_->setRange(0, SpeedLevels - 1);
    level_->setValue(SpeedLevelDefault);
    auto *choice = new QFormLayout;
    choice->addRow(QStringLiteral("Mouvement :"), motion_);
    choice->addRow(QStringLiteral("Vitesse :"), level_);

    auto runButton = [this](const QString &text, const std::function<double()> &seconds) {
        auto *b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this,
                [this, seconds] { emit trialRequested(motion(), level_->value(), seconds()); });
        runButtons_.append(b);
        return b;
    };

    // Ligne droite : 1 s puis 3 s
    auto *straight = new QWidget;
    d1_ = spin(0, 500, 0.5, 1, QStringLiteral(" cm"), 0);
    d3_ = spin(0, 500, 0.5, 1, QStringLiteral(" cm"), 0);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(runButton(QStringLiteral("Lancer 1 s"), [] { return 1.0; }));
    row1->addWidget(new QLabel(QStringLiteral("distance mesurée :")));
    row1->addWidget(d1_);
    auto *row3 = new QHBoxLayout;
    row3->addWidget(runButton(QStringLiteral("Lancer 3 s"), [] { return 3.0; }));
    row3->addWidget(new QLabel(QStringLiteral("distance mesurée :")));
    row3->addWidget(d3_);
    auto *saveStraightButton = new QPushButton(QStringLiteral("Calculer et enregistrer"));
    connect(saveStraightButton, &QPushButton::clicked, this, &CalibrationDialog::saveStraight);
    auto *straightLayout = new QVBoxLayout(straight);
    straightLayout->setContentsMargins(0, 0, 0, 0);
    straightLayout->addWidget(wrapped(QStringLiteral(
        "1. Aligner un repère du robot (l'avant du châssis) sur le 0 d'un mètre ruban posé au sol, lancer "
        "l'essai de 1 s et noter la distance.")));
    straightLayout->addLayout(row1);
    straightLayout->addWidget(wrapped(QStringLiteral("2. Replacer le robot sur le 0, lancer l'essai de 3 s.")));
    straightLayout->addLayout(row3);
    straightLayout->addWidget(saveStraightButton, 0, Qt::AlignRight);

    // Pivot : durée d'un tour complet
    auto *turn = new QWidget;
    turnTime_ = spin(0.3, 20, 0.05, 2, QStringLiteral(" s"), 2);
    auto *turnRow = new QHBoxLayout;
    turnRow->addWidget(new QLabel(QStringLiteral("Durée :")));
    turnRow->addWidget(turnTime_);
    turnRow->addWidget(runButton(QStringLiteral("Lancer"), [this] { return turnTime_->value(); }));
    auto *saveTurnButton = new QPushButton(QStringLiteral("Enregistrer : un tour complet avec cette durée"));
    connect(saveTurnButton, &QPushButton::clicked, this, &CalibrationDialog::saveTurn);
    auto *turnLayout = new QVBoxLayout(turn);
    turnLayout->setContentsMargins(0, 0, 0, 0);
    turnLayout->addWidget(wrapped(QStringLiteral(
        "Poser le robot sur une feuille avec un trait sous l'avant. Lancer, puis allonger ou raccourcir la "
        "durée jusqu'à ce que le robot revienne exactement sur le trait (un tour complet).")));
    turnLayout->addLayout(turnRow);
    turnLayout->addWidget(saveTurnButton, 0, Qt::AlignRight);

    pages_ = new QStackedWidget;
    pages_->addWidget(straight);
    pages_->addWidget(turn);

    result_ = wrapped(QString());
    result_->setStyleSheet(QStringLiteral("font-weight: bold;"));
    warning_ = wrapped(QStringLiteral("Robot non connecté : l'essai ne se fait que dans la scène."));
    warning_->setStyleSheet(QStringLiteral("color: #e57373;"));
    summary_ = new QLabel;
    summary_->setTextFormat(Qt::RichText);

    auto *reset = new QPushButton(QStringLiteral("Valeurs par défaut pour ce mouvement"));
    connect(reset, &QPushButton::clicked, this, &CalibrationDialog::resetMotion);
    auto *close = new QPushButton(QStringLiteral("Fermer"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(close);

    auto *root = new QVBoxLayout(this);
    root->addWidget(intro);
    root->addLayout(choice);
    root->addWidget(pages_);
    root->addWidget(result_);
    root->addWidget(warning_);
    root->addWidget(summary_);
    root->addLayout(buttons);

    connect(motion_, &QComboBox::currentIndexChanged, this, &CalibrationDialog::updatePage);
    connect(level_, &QSpinBox::valueChanged, this, &CalibrationDialog::updatePage);
    updatePage();
    resize(520, sizeHint().height());
}

Calibration::Motion CalibrationDialog::motion() const
{
    return Calibration::Motion(motion_->currentIndex());
}

void CalibrationDialog::setRunning(bool on)
{
    running_ = on;
    for (QPushButton *b : runButtons_)
        b->setEnabled(!on);
}

void CalibrationDialog::setConnected(bool on)
{
    warning_->setVisible(!on);
}

void CalibrationDialog::updatePage()
{
    const Calibration::Motion m = motion();
    pages_->setCurrentIndex(Calibration::isTurn(m) ? 1 : 0);
    level_->setSuffix(QStringLiteral(" (%1)").arg((level_->value() + 1) * SpeedPerLevel));
    // Proposition de durée pour un tour : d'après le calibrage actuel
    if (Calibration::isTurn(m))
        turnTime_->setValue(calibration_.duration(m, level_->value(), 360));
    result_->clear();
    updateSummary();
}

void CalibrationDialog::updateSummary()
{
    // Valeurs utilisées pour chaque vitesse, mesurées ou déduites
    const Calibration::Motion m = motion();
    const bool turn = Calibration::isTurn(m);
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='2'><tr><th align='left'>vitesse</th>"
                                  "<th align='right'>%1</th>%2<th></th></tr>")
                       .arg(turn ? QStringLiteral("°/s") : QStringLiteral("cm/s"),
                            turn ? QString() : QStringLiteral("<th align='right'>départ</th>"));
    const bool anyMeasured = !calibration_.measuredLevels(m).isEmpty();
    for (int level = 0; level < SpeedLevels; ++level) {
        const Calibration::Rate r = calibration_.rate(m, level);
        const QString origin = calibration_.isMeasured(m, level) ? QStringLiteral("<b>mesuré</b>")
                               : anyMeasured                     ? QStringLiteral("déduit")
                                                                 : QStringLiteral("par défaut");
        html += QStringLiteral("<tr><td>%1 (%2)</td><td align='right'>%3</td>%4<td>&nbsp;%5</td></tr>")
                    .arg(level)
                    .arg((level + 1) * SpeedPerLevel)
                    .arg(number(r.perS, turn ? 0 : 1),
                         turn ? QString() : QStringLiteral("<td align='right'>%1 s</td>").arg(number(r.t0, 2)), origin);
    }
    summary_->setText(html + QStringLiteral("</table>"));
}

void CalibrationDialog::saveStraight()
{
    Calibration::Rate r;
    QString error;
    if (!Calibration::fromStraight(d1_->value(), d3_->value(), &r, &error)) {
        result_->setText(error);
        return;
    }
    store(r, QStringLiteral("Enregistré : %1 cm/s, départ %2 s (vitesse %3).")
                 .arg(number(r.perS, 1), number(r.t0, 2))
                 .arg(level_->value()));
}

void CalibrationDialog::saveTurn()
{
    const double perS = 360 / turnTime_->value();
    store({perS, 0}, QStringLiteral("Enregistré : %1 °/s, un quart de tour en %2 s (vitesse %3).")
                         .arg(number(perS, 0), number(90 / perS, 2))
                         .arg(level_->value()));
}

void CalibrationDialog::store(const Calibration::Rate &r, const QString &message)
{
    calibration_.setMeasured(motion(), level_->value(), r);
    result_->setText(message);
    updateSummary();
    emit calibrationChanged(calibration_);
}

void CalibrationDialog::resetMotion()
{
    calibration_.clear(motion());
    updatePage();
    result_->setText(QStringLiteral("Valeurs par défaut rétablies pour « %1 ».").arg(Calibration::motionName(motion())));
    emit calibrationChanged(calibration_);
}

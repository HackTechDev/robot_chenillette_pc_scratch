#include "window.h"

#include "bluez.h"
#include "calibrationdialog.h"
#include "canvas.h"
#include "config.h"
#include "pairing.h"
#include "palette.h"
#include "stage.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

namespace {

const QString FileFilter = QStringLiteral("Programmes BatMobile (*.batmobile);;Tous les fichiers (*)");
const QString FileSuffix = QStringLiteral(".batmobile");
// Menu Exemples : titre, fichier dans les ressources
const QList<QPair<QString, QString>> Examples = {
    {QStringLiteral("Carré"), QStringLiteral(":/examples/carre.batmobile")},
    {QStringLiteral("Aller-retour"), QStringLiteral(":/examples/aller_retour.batmobile")},
    {QStringLiteral("Slalom"), QStringLiteral(":/examples/slalom.batmobile")},
    {QStringLiteral("Attraper un objet"), QStringLiteral(":/examples/attraper.batmobile")},
    {QStringLiteral("Danse (sans fin, ■ pour arrêter)"), QStringLiteral(":/examples/danse.batmobile")},
};

constexpr int MaxVariableName = 16;
constexpr int MaxBlockName = 24;
const QString UntitledName = QStringLiteral("sans titre");
const QString AutosaveFormat = QStringLiteral("batmobile-scratch-autosave");
constexpr int AutosaveDelayMs = 1000;

// ~/.local/share/batmobile_scratch/autosave.batmobile
QString autosavePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("autosave") + FileSuffix);
}

// ~/.local/share/batmobile_scratch/settings.json : fichiers récents
QString settingsPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("settings.json"));
}

constexpr int MaxRecentFiles = 8;

// ~/.local/share/batmobile_scratch/calibration.json
QString calibrationPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("calibration.json"));
}

const char *const CalibrationStyle = R"(
QPushButton { border: 1px solid #555; border-radius: 6px; padding: 6px 12px; }
QPushButton:hover { border-color: #888; }
QPushButton:disabled { color: #555; border-color: #333; }
QSpinBox, QDoubleSpinBox, QComboBox { background: #2a2a2a; border: 1px solid #555; border-radius: 4px; padding: 4px; }
)";

const char *const Style = R"(
QWidget { background: #111; color: #fff; }
QLabel#title { color: #FFD700; font-size: 24px; font-weight: bold; letter-spacing: 2px; }
QLabel#hint { color: #888; font-size: 11px; }
QLabel#section { color: #bbb; font-size: 12px; font-weight: bold; }
QComboBox { background: #2a2a2a; border: 1px solid #555; border-radius: 6px; padding: 6px; }
QPushButton#bt { border: 2px solid #555; border-radius: 16px; padding: 6px 20px; font-size: 14px; }
QPushButton#bt[connected="true"] { border-color: #4caf50; }
QPushButton#small { border: 1px solid #555; border-radius: 6px; padding: 6px 12px; }
QPushButton#small:hover { border-color: #888; }
QPushButton#small:disabled { color: #555; border-color: #333; }
QPushButton#run { background: #4caf50; border-radius: 20px; font-size: 20px; font-weight: bold; }
QPushButton#run:disabled { background: #2e5e30; color: #8a8a8a; }
QPushButton#step { background: #4C97FF; border-radius: 20px; font-size: 18px; font-weight: bold; }
QPushButton#step:disabled { background: #2b4f80; color: #8a8a8a; }
QPushButton#stop { background: #e53935; border-radius: 20px; font-size: 18px; font-weight: bold; }
QPushButton#quit { border: 1px solid #555; border-radius: 16px; color: #888; font-size: 15px; font-weight: bold; }
QPushButton#quit:hover { border-color: #e53935; color: #e53935; }
QMessageBox QPushButton { border: 1px solid #555; border-radius: 6px; padding: 6px 16px; min-width: 60px; }
QScrollArea { border: 1px solid #333; }
QLineEdit#search { background: #2a2a2a; border: 1px solid #555; border-radius: 6px; padding: 5px; }
)";

QLabel *label(const QString &text, const QString &name)
{
    auto *l = new QLabel(text);
    l->setObjectName(name);
    return l;
}

QPushButton *button(const QString &text, const QString &name, const QString &toolTip = {})
{
    auto *b = new QPushButton(text);
    b->setObjectName(name);
    b->setToolTip(toolTip);
    return b;
}

void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

}  // namespace

Window::Window(QWidget *parent) : QWidget(parent)
{
    setStyleSheet(Style);

    connect(&link_, &Link::connected, this, &Window::onConnected);
    connect(&link_, &Link::failed, this, &Window::onFailed);
    connect(&link_, &Link::lost, this, &Window::onLost);

    // En-tête : connexion au robot, puis exécution du programme
    deviceBox_ = new QComboBox;
    deviceBox_->setEditable(true);
    deviceBox_->setInsertPolicy(QComboBox::NoInsert);  // une adresse saisie reste du texte libre
    deviceBox_->setMinimumWidth(260);
    deviceBox_->lineEdit()->setPlaceholderText(QStringLiteral("Adresse (ex. 00:18:A1:12:16:C5)"));
    QPushButton *refresh = button(QStringLiteral("⟳"), QStringLiteral("small"),
                                  QStringLiteral("Rafraîchir les appareils appairés"));
    connect(refresh, &QPushButton::clicked, this, &Window::refreshDevices);
    btButton_ = button(QStringLiteral("Connecter"), QStringLiteral("bt"));
    btButton_->setMinimumWidth(200);
    connect(btButton_, &QPushButton::clicked, this, &Window::toggleConnection);

    runButton_ = button(QStringLiteral("▶"), QStringLiteral("run"),
                        QStringLiteral("Exécuter le script « quand ▶ est cliqué » (F5)"));
    runButton_->setFixedSize(56, 40);
    connect(runButton_, &QPushButton::clicked, this, [this] { runOrContinue(false); });
    stepButton_ = button(QStringLiteral("⏭"), QStringLiteral("step"),
                         QStringLiteral("Pas à pas : exécuter un bloc à la fois (F10)"));
    stepButton_->setFixedSize(56, 40);
    connect(stepButton_, &QPushButton::clicked, this, [this] { runOrContinue(true); });
    QPushButton *stop = button(QStringLiteral("■"), QStringLiteral("stop"), QStringLiteral("Arrêter (Échap)"));
    stop->setFixedSize(56, 40);
    connect(stop, &QPushButton::clicked, &runner_, &Runner::stop);

    auto *header = new QHBoxLayout;
    header->addWidget(label(RobotName, QStringLiteral("title")));
    header->addSpacing(16);
    header->addWidget(deviceBox_);
    header->addWidget(refresh);
    header->addWidget(btButton_);
    header->addStretch();
    header->addWidget(runButton_);
    header->addWidget(stepButton_);
    header->addWidget(stop);

    status_ = label(QString(), QStringLiteral("hint"));

    // Palette à gauche, zone de programme à droite
    palette_ = new Palette;
    auto *search = new QLineEdit;
    search->setObjectName(QStringLiteral("search"));
    search->setPlaceholderText(QStringLiteral("🔍 Rechercher un bloc"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, palette_, &Palette::setFilter);
    auto *paletteScroll = new QScrollArea;
    paletteScroll->setWidget(palette_);
    paletteScroll->setWidgetResizable(true);
    paletteScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto fitPalette = [paletteScroll, search](int width) {
        paletteScroll->setFixedWidth(width + paletteScroll->verticalScrollBar()->sizeHint().width() + 4);
        search->setFixedWidth(paletteScroll->width());
    };
    fitPalette(palette_->minimumWidth());
    connect(palette_, &Palette::widthChanged, this, fitPalette);  // nom de variable plus long
    connect(palette_, &Palette::createVariableRequested, this, &Window::createVariable);
    connect(palette_, &Palette::createBlockRequested, this, &Window::createBlock);
    connect(palette_, &Palette::deleteVariableRequested, this, [this](const QString &name) {
        if (!canvas_->removeVariable(name))
            QMessageBox::information(this, QStringLiteral("Variable"),
                                     QStringLiteral("La variable « %1 » est encore utilisée par des blocs.").arg(name));
    });

    canvas_ = new Canvas;
    auto *canvasScroll = new QScrollArea;
    canvasScroll->setWidget(canvas_);
    canvasScroll->setWidgetResizable(true);
    // Robot connecté : un simple clic ne doit pas le faire bouger par erreur
    connect(canvas_, &Canvas::scriptClicked, this, [this](const Block *first) {
        if (isReporter(first->spec().shape))
            return showValue(first);
        if (!connected_)
            run(first);
        else if (!runner_.isRunning())
            status_->setText(QStringLiteral("Robot connecté : double-clic sur un script pour l'exécuter"));
    });
    connect(canvas_, &Canvas::scriptDoubleClicked, this, [this](const Block *first) {
        if (isReporter(first->spec().shape))
            return showValue(first);
        run(first);
    });
    connect(canvas_, &Canvas::variablesChanged, this, [this](const QStringList &names) {
        palette_->setVariables(names);
        updateMonitors();
    });
    connect(&runner_, &Runner::variableChanged, this, &Window::updateMonitors);
    connect(canvas_, &Canvas::proceduresChanged, palette_, &Palette::setProcedures);
    runner_.setProcedures([this](const QString &name) { return canvas_->procedure(name); });
    connect(&runner_, &Runner::error, status_, &QLabel::setText);
    connect(canvas_, &Canvas::changed, this, [this] { setModified(canvas_->isModified()); });
    connect(&runner_, &Runner::current, canvas_, &Canvas::setCurrent);
    connect(&runner_, &Runner::finished, this, &Window::onFinished);
    connect(&runner_, &Runner::paused, this, &Window::onPaused);
    connect(canvas_, &Canvas::historyChanged, this, &Window::updateHistoryButtons);

    // Scène : le robot simulé suit les commandes du programme
    stage_ = new Stage;
    stage_->setFixedSize(360, 270);
    connect(&runner_, &Runner::sent, stage_, &Stage::command);
    connect(&runner_, &Runner::said, stage_, &Stage::setSpeech);
    QPushButton *replace = button(QStringLiteral("⟲ Replacer"), QStringLiteral("small"),
                                  QStringLiteral("Remettre le robot au centre et effacer la trace"));
    connect(replace, &QPushButton::clicked, stage_, &Stage::reset);
    QPushButton *calibrate = button(QStringLiteral("⚙ Calibrer…"), QStringLiteral("small"),
                                    QStringLiteral("Mesurer les distances et les angles du vrai robot"));
    connect(calibrate, &QPushButton::clicked, this, &Window::openCalibration);
    auto *stageButtons = new QHBoxLayout;
    stageButtons->addWidget(replace);
    stageButtons->addWidget(calibrate);
    stageButtons->addStretch();
    auto *stageCol = new QVBoxLayout;
    stageCol->setContentsMargins(12, 0, 0, 0);
    stageCol->addWidget(label(QStringLiteral("Scène (simulation)"), QStringLiteral("section")));
    stageCol->addWidget(stage_);
    stageCol->addLayout(stageButtons);
    QLabel *stageHint = label(QStringLiteral("Le robot dessiné suit le programme, robot connecté ou non. Terrain de "
                                             "160 × 120 cm ; distances et angles d'après le calibrage."),
                              QStringLiteral("hint"));
    stageHint->setWordWrap(true);
    stageHint->setFixedWidth(360);
    stageCol->addWidget(stageHint);
    stageCol->addStretch();

    auto *workspace = new QHBoxLayout;
    workspace->setSpacing(0);
    auto *paletteCol = new QVBoxLayout;
    paletteCol->setSpacing(6);
    paletteCol->addWidget(search);
    paletteCol->addWidget(paletteScroll, 1);
    workspace->addLayout(paletteCol);
    workspace->addWidget(canvasScroll, 1);
    workspace->addLayout(stageCol);

    // Bas : aide, fichiers, quitter
    QLabel *hint = label(QStringLiteral("Glisser les blocs dans la zone de programme · clic sur un nombre pour le "
                                        "modifier, molette pour l'ajuster · clic sur un script pour l'exécuter (double-clic si le robot est connecté) · "
                                        "clic droit : dupliquer / supprimer, commentaire, ranger · bloc ramené sur la palette = "
                                        "supprimé · glisser le fond : déplacer la vue · Ctrl+molette : zoom · "
                                        "Échap = arrêt"),
                         QStringLiteral("hint"));
    hint->setWordWrap(true);
    undoButton_ = button(QStringLiteral("↶"), QStringLiteral("small"), QStringLiteral("Annuler (Ctrl+Z)"));
    connect(undoButton_, &QPushButton::clicked, canvas_, &Canvas::undo);
    redoButton_ = button(QStringLiteral("↷"), QStringLiteral("small"), QStringLiteral("Rétablir (Ctrl+Y)"));
    connect(redoButton_, &QPushButton::clicked, canvas_, &Canvas::redo);
    QPushButton *zoomOut = button(QStringLiteral("−"), QStringLiteral("small"), QStringLiteral("Zoom arrière (Ctrl+−)"));
    connect(zoomOut, &QPushButton::clicked, canvas_, &Canvas::zoomOut);
    QPushButton *zoomReset = button(QStringLiteral("100 %"), QStringLiteral("small"), QStringLiteral("Zoom 100 % (Ctrl+0)"));
    zoomReset->setMinimumWidth(64);
    connect(zoomReset, &QPushButton::clicked, canvas_, &Canvas::resetZoom);
    connect(canvas_, &Canvas::zoomChanged, zoomReset,
            [zoomReset](qreal zoom) { zoomReset->setText(QStringLiteral("%1 %").arg(qRound(zoom * 100))); });
    QPushButton *zoomIn = button(QStringLiteral("+"), QStringLiteral("small"), QStringLiteral("Zoom avant (Ctrl++)"));
    connect(zoomIn, &QPushButton::clicked, canvas_, &Canvas::zoomIn);
    QPushButton *examples = button(QStringLiteral("Exemples ▾"), QStringLiteral("small"));
    auto *examplesMenu = new QMenu(examples);
    for (const auto &[title, path] : Examples)
        examplesMenu->addAction(title, this, [this, title, path] { openExample(title, path); });
    connect(examples, &QPushButton::clicked, this,
            [examples, examplesMenu] { examplesMenu->exec(examples->mapToGlobal(QPoint(0, -examplesMenu->sizeHint().height()))); });
    QPushButton *newButton = button(QStringLiteral("Nouveau"), QStringLiteral("small"));
    connect(newButton, &QPushButton::clicked, this, &Window::newProgram);
    QPushButton *openButton = button(QStringLiteral("Ouvrir…"), QStringLiteral("small"));
    connect(openButton, &QPushButton::clicked, this, &Window::openProgram);
    QPushButton *saveButton = button(QStringLiteral("Enregistrer…"), QStringLiteral("small"));
    connect(saveButton, &QPushButton::clicked, this, &Window::saveProgram);
    // Plus ▾ : fichiers récents, export en image (menu reconstruit à chaque ouverture)
    QPushButton *more = button(QStringLiteral("Plus ▾"), QStringLiteral("small"));
    connect(more, &QPushButton::clicked, this, [this, more] {
        QMenu menu(this);
        fillMoreMenu(&menu);
        menu.exec(more->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    QPushButton *quit = button(QStringLiteral("✕"), QStringLiteral("quit"), QStringLiteral("Quitter"));
    quit->setFixedSize(32, 32);
    connect(quit, &QPushButton::clicked, this, &QWidget::close);  // confirmation dans closeEvent

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(hint, 1);
    bottom->addWidget(undoButton_);
    bottom->addWidget(redoButton_);
    bottom->addSpacing(12);
    bottom->addWidget(zoomOut);
    bottom->addWidget(zoomReset);
    bottom->addWidget(zoomIn);
    bottom->addSpacing(12);
    bottom->addWidget(examples);
    bottom->addWidget(newButton);
    bottom->addWidget(openButton);
    bottom->addWidget(saveButton);
    bottom->addWidget(more);
    bottom->addSpacing(12);
    bottom->addWidget(quit);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    root->addLayout(header);
    root->addWidget(status_);
    root->addLayout(workspace, 1);
    root->addLayout(bottom);

    // Espace ou Entrée ne doivent pas relancer un bouton resté sélectionné
    for (QPushButton *b : findChildren<QPushButton *>())
        b->setFocusPolicy(Qt::NoFocus);
    deviceBox_->setFocusPolicy(Qt::ClickFocus);

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, &runner_, &Runner::stop);
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, [this] { runOrContinue(false); });
    connect(new QShortcut(QKeySequence(Qt::Key_F10), this), &QShortcut::activated, this, [this] { runOrContinue(true); });
    connect(new QShortcut(QKeySequence::New, this), &QShortcut::activated, this, &Window::newProgram);
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, &Window::openProgram);
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, &Window::saveProgram);
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, canvas_, &Canvas::undo);
    // Presse-papiers : une zone de saisie qui a le focus garde ces raccourcis pour son texte
    connect(new QShortcut(QKeySequence::Copy, this), &QShortcut::activated, canvas_, &Canvas::copySelection);
    connect(new QShortcut(QKeySequence::Cut, this), &QShortcut::activated, canvas_, &Canvas::cutSelection);
    connect(new QShortcut(QKeySequence::Paste, this), &QShortcut::activated, canvas_, &Canvas::paste);
    connect(new QShortcut(QKeySequence::Delete, this), &QShortcut::activated, canvas_, &Canvas::deleteSelection);
    connect(new QShortcut(QKeySequence::SelectAll, this), &QShortcut::activated, canvas_, &Canvas::selectAll);
    connect(new QShortcut(QKeySequence::Redo, this), &QShortcut::activated, canvas_, &Canvas::redo);
    connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated, canvas_, &Canvas::zoomIn);
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), this), &QShortcut::activated, canvas_,
            &Canvas::zoomIn);  // Ctrl+= : « + » sans Maj sur un clavier QWERTY
    connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated, canvas_, &Canvas::zoomOut);
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated, canvas_,
            &Canvas::resetZoom);
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this), &QShortcut::activated, canvas_, &Canvas::redo);

    autosaveTimer_.setSingleShot(true);
    autosaveTimer_.setInterval(AutosaveDelayMs);
    connect(&autosaveTimer_, &QTimer::timeout, this, &Window::autosave);

    qApp->installEventFilter(this);  // touches reçues quel que soit le widget qui a le focus

    calibration_.load(calibrationPath());
    applyCalibration();
    loadSettings();

    refreshDevices();
    restoreAutosave();
    setModified(canvas_->isModified());
    updateHistoryButtons();
}

// --- Connexion ---

void Window::refreshDevices()
{
    const QString current = deviceBox_->currentText();
    deviceBox_->clear();
    const QList<bluez::Device> devices = bluez::pairedDevices();
    int robot = -1;
    for (const bluez::Device &d : devices) {
        if (robot < 0 && d.name == QLatin1String(RobotName))
            robot = deviceBox_->count();
        deviceBox_->addItem(QStringLiteral("%1 — %2").arg(d.name, d.address), d.address);
    }
    if (robot >= 0) {
        deviceBox_->setCurrentIndex(robot);
    } else {
        // Robot pas encore appairé : « Connecter » lancera l'appairage automatique
        deviceBox_->insertItem(0, QStringLiteral("%1 — appairage automatique").arg(RobotName), QVariant());
        deviceBox_->setCurrentIndex(0);
        if (!current.isEmpty() && current != deviceBox_->itemText(0))
            deviceBox_->setEditText(current);
    }
}

QString Window::selectedAddress() const
{
    const QString text = deviceBox_->currentText();
    const int i = deviceBox_->currentIndex();
    if (i >= 0 && text == deviceBox_->itemText(i))
        return deviceBox_->currentData().toString();
    static const QRegularExpression re(QStringLiteral("([0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5})"));
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

bool Window::autoPairingSelected() const
{
    const int i = deviceBox_->currentIndex();
    return i >= 0 && !deviceBox_->currentData().isValid() && deviceBox_->currentText() == deviceBox_->itemText(i);
}

void Window::toggleConnection()
{
    if (connected_) {
        runner_.stop();
        link_.disconnectFromRobot();
        setConnected(false);
        return;
    }
    if (autoPairingSelected())
        return startPairing();
    const QString address = selectedAddress();
    if (address.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Appareil"),
                             QStringLiteral("Choisir un appareil appairé ou saisir une adresse Bluetooth."));
        return;
    }
    connectTo(address);
}

void Window::connectTo(const QString &address)
{
    btButton_->setText(QStringLiteral("Connexion…"));
    btButton_->setEnabled(false);
    deviceBox_->setEnabled(false);
    status_->clear();
    link_.connectTo(address, char('0' + SpeedLevelDefault));
}

void Window::startPairing()
{
    // Recherche, PIN, confiance, puis connexion au robot
    btButton_->setText(QStringLiteral("Appairage…"));
    btButton_->setEnabled(false);
    deviceBox_->setEnabled(false);
    status_->setText(QStringLiteral("Recherche de %1…").arg(RobotName));
    repaint();  // les premiers appels D-Bus sont bloquants

    pairing_ = new Pairing(false, 30, this);
    connect(pairing_, &Pairing::progress, status_, &QLabel::setText);
    connect(pairing_, &Pairing::succeeded, this, [this](const QString &address) {
        pairing_->deleteLater();
        pairing_ = nullptr;
        refreshDevices();
        connectTo(address);
    });
    connect(pairing_, &Pairing::failed, this, [this](const QString &error) {
        pairing_->deleteLater();
        pairing_ = nullptr;
        setConnected(false);
        status_->clear();
        QMessageBox::critical(this, QStringLiteral("Appairage échoué"), error);
    });
    pairing_->start();
}

void Window::onConnected(const QString &address)
{
    connectedName_ = deviceBox_->currentText().section(QStringLiteral(" — "), 0, 0);
    if (connectedName_.isEmpty())
        connectedName_ = address;
    setConnected(true);
    status_->setText(QStringLiteral("Robot connecté : ▶ pour exécuter le programme"));
}

void Window::onFailed(const QString &error)
{
    setConnected(false);
    status_->clear();
    QMessageBox::critical(this, QStringLiteral("Connexion échouée"), error);
}

void Window::onLost()
{
    runner_.stop();
    setConnected(false);
    QMessageBox::warning(this, QStringLiteral("Liaison perdue"),
                         QStringLiteral("La connexion Bluetooth avec le robot a été perdue."));
}

void Window::setConnected(bool on)
{
    connected_ = on;
    if (calibrationDialog_)
        calibrationDialog_->setConnected(on);
    btButton_->setEnabled(true);
    btButton_->setText(on ? QStringLiteral("Connecté — %1").arg(connectedName_) : QStringLiteral("Connecter"));
    btButton_->setProperty("connected", on);
    repolish(btButton_);
    deviceBox_->setEnabled(!on);
}

// --- Exécution ---

bool Window::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() != QEvent::KeyPress || QApplication::activeWindow() != this || QApplication::activePopupWidget()
        || qobject_cast<QLineEdit *>(QApplication::focusWidget())
        || qobject_cast<QPlainTextEdit *>(QApplication::focusWidget()))  // saisie d'un nombre ou d'un commentaire
        return QWidget::eventFilter(o, e);
    const auto *k = static_cast<QKeyEvent *>(e);
    if (k->isAutoRepeat() || (k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        return QWidget::eventFilter(o, e);
    const int key = keyIndex(k->key());
    const Block *first = key >= 0 ? canvas_->keyScript(key) : nullptr;
    if (!first)
        return QWidget::eventFilter(o, e);  // touche sans script : comportement normal (flèches…)
    runKeyScript(first, key);
    return true;
}

void Window::runKeyScript(const Block *first, int key)
{
    if (runner_.isRunning()) {
        // Programme ▶ ou pas à pas en cours : les touches ne l'interrompent pas
        if (!keyRun_ || runner_.isPaused())
            return;
        runner_.stop();  // une nouvelle touche remplace le script de touche en cours
    }
    if (startRun(first, false, false,
                 QStringLiteral("Touche « %1 » : script en cours… (■ ou Échap pour arrêter)").arg(keyName(key))))
        keyRun_ = runner_.isRunning();
}

void Window::runOrContinue(bool stepByStep)
{
    if (!runner_.isPaused())
        return run(canvas_->startScript(), stepByStep);
    setRunButtonsEnabled(false);
    status_->setText(stepByStep ? QStringLiteral("Pas à pas : bloc en cours…")
                                : QStringLiteral("Programme en cours… (■ ou Échap pour arrêter)"));
    runner_.resume(stepByStep);
}

void Window::setRunButtonsEnabled(bool on)
{
    runButton_->setEnabled(on);
    stepButton_->setEnabled(on);
}

void Window::onPaused()
{
    setRunButtonsEnabled(true);
    status_->setText(QStringLiteral("Pas à pas : ⏭ exécute le bloc entouré · ▶ continue · ■ arrête"));
}

void Window::run(const Block *first, bool stepByStep)
{
    if (runner_.isRunning())
        return;
    startRun(first, stepByStep, true,
             connected_ ? QStringLiteral("Programme en cours… (■ ou Échap pour arrêter)")
                        : QStringLiteral("Simulation, robot non connecté… (■ ou Échap pour arrêter)"));
}

bool Window::startRun(const Block *first, bool stepByStep, bool resetSpeed, const QString &status)
{
    if (!first) {
        QMessageBox::information(this, QStringLiteral("Programme"),
                                 QStringLiteral("Ajouter le bloc « quand ▶ est cliqué » en haut d'un script."));
        return false;
    }
    keyRun_ = false;
    canvas_->setRunning(true);
    canvas_->setFocus();  // termine une saisie en cours
    palette_->setLocked(true);
    setRunButtonsEnabled(false);
    status_->setText(status);  // avant start : un programme instantané se termine tout de suite
    runner_.start(first, stepByStep, resetSpeed);
    return true;
}

void Window::openCalibration()
{
    if (!calibrationDialog_) {
        calibrationDialog_ = new CalibrationDialog(calibration_, this);
        calibrationDialog_->setStyleSheet(CalibrationStyle);
        connect(calibrationDialog_, &CalibrationDialog::trialRequested, this, &Window::runTrial);
        connect(calibrationDialog_, &CalibrationDialog::calibrationChanged, this, [this](const Calibration &c) {
            calibration_ = c;
            applyCalibration();
            if (!calibration_.save(calibrationPath()))
                status_->setText(QStringLiteral("Calibrage non enregistré : %1").arg(calibrationPath()));
        });
    }
    calibrationDialog_->setConnected(connected_);
    calibrationDialog_->setRunning(runner_.isRunning());
    calibrationDialog_->show();
    calibrationDialog_->raise();
    calibrationDialog_->activateWindow();
}

void Window::applyCalibration()
{
    runner_.setCalibration(calibration_);
    stage_->setCalibration(calibration_);
}

void Window::runTrial(Calibration::Motion motion, int level, double seconds)
{
    if (runner_.isRunning())
        return;
    // Petit programme : vitesse, puis le mouvement pendant la durée demandée
    static const Kind Timed[] = {Kind::Forward, Kind::Backward, Kind::Left, Kind::Right};
    trial_ = std::make_unique<Block>(Kind::Speed);
    trial_->inputs[0].value = level;
    trial_->next = std::make_unique<Block>(Timed[motion]);
    trial_->next->inputs[0].value = seconds;
    stage_->reset();
    startRun(trial_.get(), false, true,
             QStringLiteral("Essai de calibrage : %1 pendant %2 s, vitesse %3")
                 .arg(Calibration::motionName(motion), formatValue(Param::Seconds, seconds))
                 .arg(level));
    if (calibrationDialog_)
        calibrationDialog_->setRunning(runner_.isRunning());
}

void Window::onFinished()
{
    if (calibrationDialog_)
        calibrationDialog_->setRunning(false);
    canvas_->setRunning(false);
    palette_->setLocked(false);
    setRunButtonsEnabled(true);
    status_->setText(QStringLiteral("Programme terminé"));
}

// --- Fichiers ---

void Window::setModified(bool on)
{
    modified_ = on;
    const QString name = fileName_.isEmpty() ? untitled_ : QFileInfo(fileName_).fileName();
    setWindowTitle(QStringLiteral("%1 — %2%3").arg(RobotName, name, on ? QStringLiteral(" *") : QString()));
    autosaveTimer_.start();
}

void Window::updateHistoryButtons()
{
    undoButton_->setEnabled(canvas_->canUndo());
    redoButton_->setEnabled(canvas_->canRedo());
}

// Sauvegarde automatique : le programme en cours (enregistré ou non) est retrouvé au
// prochain lancement, même après un plantage
void Window::autosave()
{
    if (canvas_->hasGhost()) {
        autosaveTimer_.start();  // glisser-déposer en cours : plus tard
        return;
    }
    autosaveTimer_.stop();
    const QJsonObject o{
        {QStringLiteral("format"), AutosaveFormat},
        {QStringLiteral("program"), canvas_->save()},
        {QStringLiteral("saved"), canvas_->savedState()},
        {QStringLiteral("fileName"), fileName_},
        {QStringLiteral("untitled"), untitled_},
    };
    const QString path = autosavePath();
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(o).toJson());
    if (!file.commit())
        qWarning("Sauvegarde automatique impossible : %s", qPrintable(file.errorString()));
}

void Window::restoreAutosave()
{
    QFile file(autosavePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o[QStringLiteral("format")].toString() != AutosaveFormat
        || !canvas_->load(o[QStringLiteral("program")].toObject()))
        return;
    canvas_->setSavedState(o[QStringLiteral("saved")].toObject());
    fileName_ = o[QStringLiteral("fileName")].toString();
    untitled_ = o[QStringLiteral("untitled")].toString(UntitledName);
    status_->setText(QStringLiteral("Programme de la dernière séance retrouvé"));
}

void Window::createVariable()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Nouvelle variable"),
                                               QStringLiteral("Nom de la variable :"), QLineEdit::Normal, QString(), &ok)
                             .trimmed()
                             .left(MaxVariableName);
    if (!ok || name.isEmpty())
        return;
    if (!canvas_->addVariable(name))
        QMessageBox::information(this, QStringLiteral("Variable"),
                                 QStringLiteral("La variable « %1 » existe déjà.").arg(name));
}

void Window::createBlock()
{
    // Nom et paramètres facultatifs (ex. « faire un carré », paramètre « côté »)
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Nouveau bloc"));
    dialog.setStyleSheet(CalibrationStyle);
    auto *name = new QLineEdit;
    name->setPlaceholderText(QStringLiteral("ex. faire un carré"));
    name->setMaxLength(MaxBlockName);
    QList<QLineEdit *> params;
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Nom du bloc :"), name);
    for (int i = 0; i < MaxParams; ++i) {
        params.append(new QLineEdit);
        params.last()->setPlaceholderText(i == 0 ? QStringLiteral("facultatif, ex. côté") : QStringLiteral("facultatif"));
        params.last()->setMaxLength(MaxVariableName);
        form->addRow(QStringLiteral("Paramètre %1 :").arg(i + 1), params.last());
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Créer"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(new QLabel(QStringLiteral("Les paramètres deviennent des cases à remplir dans le bloc,\n"
                                                "et des valeurs à utiliser dans sa définition.")));
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    createProcedure(name->text(), {params[0]->text(), params[1]->text()});
}

bool Window::createProcedure(const QString &rawName, const QStringList &rawParams)
{
    const QString name = rawName.trimmed();
    QStringList params;
    for (const QString &p : rawParams) {
        if (!p.trimmed().isEmpty() && !params.contains(p.trimmed()))
            params.append(p.trimmed());
    }
    if (name.isEmpty())
        return false;
    if (!canvas_->addProcedure(name, params)) {
        QMessageBox::information(this, QStringLiteral("Mes blocs"),
                                 QStringLiteral("Le bloc « %1 » existe déjà.").arg(name));
        return false;
    }
    status_->setText(QStringLiteral("Empiler les blocs sous « définir %1 », puis utiliser « %1 » depuis la palette")
                         .arg(name));
    return true;
}

void Window::updateMonitors()
{
    QList<QPair<QString, double>> monitors;
    for (const QString &name : canvas_->variables())
        monitors.append({name, runner_.variable(name)});
    stage_->setMonitors(monitors);
}

void Window::showValue(const Block *reporter)
{
    // Clic sur un reporter seul : sa valeur, comme la bulle de Scratch
    const double v = runner_.evaluate(reporter);
    const bool boolean = reporter->spec().shape == Shape::Boolean;
    status_->setText(QStringLiteral("Valeur : %1")
                         .arg(boolean ? (v != 0 ? QStringLiteral("vrai") : QStringLiteral("faux")) : formatNumber(v)));
}

void Window::programLoaded()
{
    runner_.resetVariables();  // nouveau programme : variables à zéro
    updateMonitors();
}

void Window::newProgram()
{
    runner_.stop();
    if (!confirmDiscard())
        return;
    canvas_->clear();
    programLoaded();
    fileName_.clear();
    untitled_ = UntitledName;
    setModified(false);
}

void Window::openExample(const QString &title, const QString &path)
{
    runner_.stop();
    if (!confirmDiscard())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !canvas_->load(QJsonDocument::fromJson(file.readAll()).object()))
        return;
    programLoaded();
    // Pas de fichier : « Enregistrer… » demandera où le copier
    fileName_.clear();
    untitled_ = QStringLiteral("%1 (exemple)").arg(title.section(QStringLiteral(" ("), 0, 0));
    setModified(false);
    stage_->reset();
}

void Window::openProgram()
{
    runner_.stop();
    if (!confirmDiscard())
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un programme"),
                                                      QFileInfo(fileName_).path(), FileFilter);
    if (!path.isEmpty())
        openFile(path);
}

void Window::openRecent(const QString &path)
{
    runner_.stop();
    if (!QFileInfo::exists(path)) {
        QMessageBox::information(this, QStringLiteral("Fichiers récents"),
                                 QStringLiteral("%1 n'existe plus : retiré de la liste.").arg(path));
        recentFiles_.removeAll(path);
        saveSettings();
        return;
    }
    if (confirmDiscard())
        openFile(path);
}

bool Window::openFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Ouvrir"), file.errorString());
        return false;
    }
    if (!canvas_->load(QJsonDocument::fromJson(file.readAll()).object())) {
        QMessageBox::critical(this, QStringLiteral("Ouvrir"),
                              QStringLiteral("%1 n'est pas un programme BatMobile.").arg(QFileInfo(path).fileName()));
        return false;
    }
    programLoaded();
    fileName_ = path;
    setModified(false);
    addRecent(path);
    return true;
}

// --- Fichiers récents, export en image ---

void Window::fillMoreMenu(QMenu *menu)
{
    QMenu *recent = menu->addMenu(QStringLiteral("Fichiers récents"));
    for (const QString &path : std::as_const(recentFiles_)) {
        QAction *a = recent->addAction(QFileInfo(path).fileName(), this, [this, path] { openRecent(path); });
        a->setToolTip(path);
        a->setStatusTip(path);
    }
    if (recentFiles_.isEmpty()) {
        recent->addAction(QStringLiteral("(aucun)"))->setEnabled(false);
    } else {
        recent->addSeparator();
        recent->addAction(QStringLiteral("Effacer la liste"), this, [this] {
            recentFiles_.clear();
            saveSettings();
        });
    }
    recent->setToolTipsVisible(true);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Exporter en image (PNG)…"), this, &Window::exportImage);
}

void Window::addRecent(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    recentFiles_.removeAll(absolute);
    recentFiles_.prepend(absolute);
    while (recentFiles_.size() > MaxRecentFiles)
        recentFiles_.removeLast();
    saveSettings();
}

void Window::loadSettings()
{
    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    recentFiles_.clear();
    for (const QJsonValue &v : o[QStringLiteral("recentFiles")].toArray()) {
        if (recentFiles_.size() < MaxRecentFiles && !v.toString().isEmpty())
            recentFiles_.append(v.toString());
    }
}

void Window::saveSettings() const
{
    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("recentFiles"), QJsonArray::fromStringList(recentFiles_)}})
                       .toJson());
        file.commit();
    }
}

bool Window::exportImage()
{
    const QImage image = canvas_->renderImage();
    if (image.isNull()) {
        QMessageBox::information(this, QStringLiteral("Exporter"), QStringLiteral("Le programme est vide."));
        return false;
    }
    const QString base = fileName_.isEmpty() ? QDir::home().filePath(QStringLiteral("programme"))
                                             : QFileInfo(fileName_).path() + QLatin1Char('/') + QFileInfo(fileName_).completeBaseName();
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter le programme en image"),
                                                base + QStringLiteral(".png"), QStringLiteral("Image PNG (*.png)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".png");
    if (!image.save(path, "PNG")) {
        QMessageBox::critical(this, QStringLiteral("Exporter"), QStringLiteral("Impossible d'écrire %1.").arg(path));
        return false;
    }
    status_->setText(QStringLiteral("Image enregistrée : %1").arg(path));
    return true;
}

bool Window::saveProgram()
{
    const QString start = fileName_.isEmpty() ? QDir::home().filePath(QStringLiteral("programme") + FileSuffix)
                                              : fileName_;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Enregistrer le programme"), start, FileFilter);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += FileSuffix;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(QJsonDocument(canvas_->save()).toJson()) < 0) {
        QMessageBox::critical(this, QStringLiteral("Enregistrer"), file.errorString());
        return false;
    }
    fileName_ = path;
    canvas_->markSaved();
    setModified(false);
    addRecent(path);
    return true;
}

bool Window::confirmDiscard()
{
    if (!modified_)
        return true;
    QMessageBox box(QMessageBox::Question, QStringLiteral("Programme modifié"),
                    QStringLiteral("Le programme n'est pas enregistré."), QMessageBox::NoButton, this);
    QPushButton *save = box.addButton(QStringLiteral("Enregistrer…"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(QStringLiteral("Ne pas enregistrer"), QMessageBox::DestructiveRole);
    box.setDefaultButton(box.addButton(QStringLiteral("Annuler"), QMessageBox::RejectRole));
    box.exec();
    if (box.clickedButton() == save)
        return saveProgram();
    return box.clickedButton() == discard;
}

bool Window::confirmQuit()
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("Quitter"),
                    QStringLiteral("Quitter %1 Scratch ?").arg(RobotName), QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(QStringLiteral("Quitter"), QMessageBox::AcceptRole);
    box.setDefaultButton(box.addButton(QStringLiteral("Annuler"), QMessageBox::RejectRole));
    box.exec();
    return box.clickedButton() == yes;
}

void Window::closeEvent(QCloseEvent *e)
{
    // Bouton ✕ comme croix de la barre de titre : confirmation avant de quitter. Rien n'est
    // perdu : le programme, même non enregistré, est sauvegardé automatiquement.
    runner_.stop();
    if (!confirmQuit()) {
        e->ignore();
        return;
    }
    autosave();
    delete pairing_;  // annule un appairage en cours
    pairing_ = nullptr;
    link_.disconnectFromRobot();  // envoie un stop avant de fermer
    QWidget::closeEvent(e);
}

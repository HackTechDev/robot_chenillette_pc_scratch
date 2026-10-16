#include "window.h"

#include "bluez.h"
#include "canvas.h"
#include "config.h"
#include "pairing.h"
#include "palette.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QVBoxLayout>

namespace {

const QString FileFilter = QStringLiteral("Programmes BatMobile (*.batmobile);;Tous les fichiers (*)");
const QString FileSuffix = QStringLiteral(".batmobile");

const char *const Style = R"(
QWidget { background: #111; color: #fff; }
QLabel#title { color: #FFD700; font-size: 24px; font-weight: bold; letter-spacing: 2px; }
QLabel#hint { color: #888; font-size: 11px; }
QComboBox { background: #2a2a2a; border: 1px solid #555; border-radius: 6px; padding: 6px; }
QPushButton#bt { border: 2px solid #555; border-radius: 16px; padding: 6px 20px; font-size: 14px; }
QPushButton#bt[connected="true"] { border-color: #4caf50; }
QPushButton#small { border: 1px solid #555; border-radius: 6px; padding: 6px 12px; }
QPushButton#small:hover { border-color: #888; }
QPushButton#run { background: #4caf50; border-radius: 20px; font-size: 20px; font-weight: bold; }
QPushButton#run:disabled { background: #2e5e30; color: #8a8a8a; }
QPushButton#stop { background: #e53935; border-radius: 20px; font-size: 18px; font-weight: bold; }
QPushButton#quit { border: 1px solid #555; border-radius: 16px; color: #888; font-size: 15px; font-weight: bold; }
QPushButton#quit:hover { border-color: #e53935; color: #e53935; }
QMessageBox QPushButton { border: 1px solid #555; border-radius: 6px; padding: 6px 16px; min-width: 60px; }
QScrollArea { border: 1px solid #333; }
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
                        QStringLiteral("Exécuter le script « quand ▶ est cliqué »"));
    runButton_->setFixedSize(56, 40);
    connect(runButton_, &QPushButton::clicked, this, [this] { run(canvas_->startScript()); });
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
    header->addWidget(stop);

    status_ = label(QString(), QStringLiteral("hint"));

    // Palette à gauche, zone de programme à droite
    palette_ = new Palette;
    auto *paletteScroll = new QScrollArea;
    paletteScroll->setWidget(palette_);
    paletteScroll->setWidgetResizable(true);
    paletteScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    paletteScroll->setFixedWidth(palette_->minimumWidth() + paletteScroll->verticalScrollBar()->sizeHint().width() + 4);

    canvas_ = new Canvas;
    auto *canvasScroll = new QScrollArea;
    canvasScroll->setWidget(canvas_);
    canvasScroll->setWidgetResizable(true);
    connect(canvas_, &Canvas::runRequested, this, &Window::run);
    connect(canvas_, &Canvas::changed, this, [this] { setModified(true); });
    connect(&runner_, &Runner::current, canvas_, &Canvas::setCurrent);
    connect(&runner_, &Runner::finished, this, &Window::onFinished);

    auto *workspace = new QHBoxLayout;
    workspace->setSpacing(0);
    workspace->addWidget(paletteScroll);
    workspace->addWidget(canvasScroll, 1);

    // Bas : aide, fichiers, quitter
    QLabel *hint = label(QStringLiteral("Glisser les blocs dans la zone de programme · clic sur un nombre pour le "
                                        "modifier · clic sur un script pour l'exécuter · clic droit : dupliquer / "
                                        "supprimer · bloc ramené sur la palette = supprimé · Échap = arrêt"),
                         QStringLiteral("hint"));
    hint->setWordWrap(true);
    QPushButton *newButton = button(QStringLiteral("Nouveau"), QStringLiteral("small"));
    connect(newButton, &QPushButton::clicked, this, &Window::newProgram);
    QPushButton *openButton = button(QStringLiteral("Ouvrir…"), QStringLiteral("small"));
    connect(openButton, &QPushButton::clicked, this, &Window::openProgram);
    QPushButton *saveButton = button(QStringLiteral("Enregistrer…"), QStringLiteral("small"));
    connect(saveButton, &QPushButton::clicked, this, &Window::saveProgram);
    QPushButton *quit = button(QStringLiteral("✕"), QStringLiteral("quit"), QStringLiteral("Quitter"));
    quit->setFixedSize(32, 32);
    connect(quit, &QPushButton::clicked, this, &QWidget::close);  // confirmation dans closeEvent

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(hint, 1);
    bottom->addWidget(newButton);
    bottom->addWidget(openButton);
    bottom->addWidget(saveButton);
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
    connect(new QShortcut(QKeySequence::New, this), &QShortcut::activated, this, &Window::newProgram);
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, &Window::openProgram);
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, &Window::saveProgram);

    refreshDevices();
    setModified(false);
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
    btButton_->setEnabled(true);
    btButton_->setText(on ? QStringLiteral("Connecté — %1").arg(connectedName_) : QStringLiteral("Connecter"));
    btButton_->setProperty("connected", on);
    repolish(btButton_);
    deviceBox_->setEnabled(!on);
}

// --- Exécution ---

void Window::run(const Block *first)
{
    if (runner_.isRunning())
        return;
    if (!first) {
        QMessageBox::information(this, QStringLiteral("Programme"),
                                 QStringLiteral("Ajouter le bloc « quand ▶ est cliqué » en haut d'un script."));
        return;
    }
    canvas_->setRunning(true);
    palette_->setLocked(true);
    runButton_->setEnabled(false);
    status_->setText(connected_ ? QStringLiteral("Programme en cours… (■ ou Échap pour arrêter)")
                                : QStringLiteral("Simulation, robot non connecté… (■ ou Échap pour arrêter)"));
    runner_.start(first);
}

void Window::onFinished()
{
    canvas_->setRunning(false);
    palette_->setLocked(false);
    runButton_->setEnabled(true);
    status_->setText(QStringLiteral("Programme terminé"));
}

// --- Fichiers ---

void Window::setModified(bool on)
{
    modified_ = on;
    const QString name = fileName_.isEmpty() ? QStringLiteral("sans titre") : QFileInfo(fileName_).fileName();
    setWindowTitle(QStringLiteral("%1 — %2%3").arg(RobotName, name, on ? QStringLiteral(" *") : QString()));
}

void Window::newProgram()
{
    runner_.stop();
    if (!confirmDiscard())
        return;
    canvas_->clear();
    fileName_.clear();
    setModified(false);
}

void Window::openProgram()
{
    runner_.stop();
    if (!confirmDiscard())
        return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un programme"),
                                                      QFileInfo(fileName_).path(), FileFilter);
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("Ouvrir"), file.errorString());
        return;
    }
    if (!canvas_->load(QJsonDocument::fromJson(file.readAll()).object())) {
        QMessageBox::critical(this, QStringLiteral("Ouvrir"),
                              QStringLiteral("%1 n'est pas un programme BatMobile.").arg(QFileInfo(path).fileName()));
        return;
    }
    fileName_ = path;
    setModified(false);
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
    setModified(false);
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
    // Bouton ✕ comme croix de la barre de titre : confirmation avant de quitter
    runner_.stop();
    if (!(modified_ ? confirmDiscard() : confirmQuit())) {
        e->ignore();
        return;
    }
    delete pairing_;  // annule un appairage en cours
    pairing_ = nullptr;
    link_.disconnectFromRobot();  // envoie un stop avant de fermer
    QWidget::closeEvent(e);
}

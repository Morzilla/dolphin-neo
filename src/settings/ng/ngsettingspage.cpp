/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ngsettingspage.h"

#include "kitemviews/kitemlistwidget.h"
#include "dolphin_contentdisplaysettings.h"

#include <KColorButton>
#include <KUrlRequester>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

// The labels are in Italian for now, like the other dolphin-ng strings; they should go through i18n.

namespace
{
QSlider *percentSlider(QWidget *parent, int from, int to)
{
    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(from, to);
    slider->setTickPosition(QSlider::NoTicks);
    return slider;
}
QGroupBox *group(const QString &title, QVBoxLayout *into, QFormLayout **form)
{
    auto *box = new QGroupBox(title);
    *form = new QFormLayout(box);
    (*form)->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    into->addWidget(box);
    return box;
}
}

NgSettingsPage::NgSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // ---- Barra laterale ---------------------------------------------------------------------------------
    group(QStringLiteral("Barra laterale"), layout, &m_sidebarForm);
    QFormLayout *form = m_sidebarForm;

    m_glassPanel = new QCheckBox(QStringLiteral("Pannello a vetro con sfondo"), this);
    form->addRow(QString(), m_glassPanel);

    m_backdrop = new QComboBox(this);
    m_backdrop->addItem(QStringLiteral("Immagine sfocata"), QString());
    m_backdrop->addItem(QStringLiteral("Gradiente animato"), QStringLiteral("lava"));
    m_backdrop->addItem(QStringLiteral("Blur vero del desktop (compositor)"), QStringLiteral("live"));
    form->addRow(QStringLiteral("Sfondo:"), m_backdrop);

    // picture
    m_image = new KUrlRequester(this);
    m_image->setMode(KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
    m_image->setPlaceholderText(QStringLiteral("Sfondo del desktop"));
    m_image->setToolTip(QStringLiteral("Immagine mostrata, sfocata, dietro la barra laterale. Vuoto: lo sfondo del desktop di ogni schermo."));
    form->addRow(QStringLiteral("Immagine:"), m_image);
    m_glass = percentSlider(this, 0, 100);
    form->addRow(QStringLiteral("Quanto si vede:"), m_glass);
    m_blur = percentSlider(this, 0, 100);
    form->addRow(QStringLiteral("Sfocatura:"), m_blur);
    m_motion = new QCheckBox(QStringLiteral("L'immagine si muove lentamente (zoom e scorrimento)"), this);
    form->addRow(QString(), m_motion);

    // animated gradient
    m_lavaStyle = new QComboBox(this);
    m_lavaStyle->addItem(QStringLiteral("Gradiente che scorre"), QString());
    m_lavaStyle->addItem(QStringLiteral("Macchie (lava lamp)"), QStringLiteral("blobs"));
    form->addRow(QStringLiteral("Animazione:"), m_lavaStyle);
    m_lavaSize = percentSlider(this, 10, 100);
    form->addRow(QStringLiteral("Ampiezza delle zone di colore:"), m_lavaSize);
    m_lavaColours = new QComboBox(this);
    m_lavaColours->addItem(QStringLiteral("Petrolio, ambra e indaco"), QString());
    m_lavaColours->addItem(QStringLiteral("Dallo sfondo del desktop"), QStringLiteral("wallpaper"));
    m_lavaColours->addItem(QStringLiteral("Personalizzati"), QStringLiteral("custom"));
    form->addRow(QStringLiteral("Colori:"), m_lavaColours);
    m_lavaColourRow = new QWidget(this);
    auto *colourRow = new QHBoxLayout(m_lavaColourRow);
    colourRow->setContentsMargins(0, 0, 0, 0);
    for (KColorButton *&button : m_lavaColour) {
        button = new KColorButton(m_lavaColourRow);
        colourRow->addWidget(button);
        connect(button, &KColorButton::changed, this, &NgSettingsPage::changed);
    }
    colourRow->addStretch();
    form->addRow(QString(), m_lavaColourRow);

    // shared
    m_lavaSpeed = percentSlider(this, 5, 100);
    form->addRow(QStringLiteral("Velocità del movimento:"), m_lavaSpeed);

    m_rim = new QComboBox(this);
    m_rim->addItem(QStringLiteral("Nessuno"), QString());
    m_rim->addItem(QStringLiteral("Riflesso in diagonale"), QStringLiteral("diagonal"));
    m_rim->addItem(QStringLiteral("Linea sottile uniforme"), QStringLiteral("soft"));
    m_rim->addItem(QStringLiteral("Smusso (scuro fuori, chiaro dentro)"), QStringLiteral("bevel"));
    m_rim->addItem(QStringLiteral("Alone sfumato"), QStringLiteral("glow"));
    m_rim->addItem(QStringLiteral("Luce dall'alto"), QStringLiteral("top"));
    form->addRow(QStringLiteral("Bordo del pannello:"), m_rim);

    m_devices = new QCheckBox(QStringLiteral("Mostra la sezione «Dispositivi» (altrimenti c'è il riquadro del disco)"), this);
    form->addRow(QString(), m_devices);

    // ---- Elenco dei file --------------------------------------------------------------------------------
    group(QStringLiteral("Elenco dei file (vista dettagli)"), layout, &m_listForm);
    form = m_listForm;

    m_twoLines = new QCheckBox(QStringLiteral("Righe su due linee, senza colonne"), this);
    form->addRow(QString(), m_twoLines);

    m_rows = new QComboBox(this);
    m_rows->addItem(QStringLiteral("Schede"), QStringLiteral("card"));
    m_rows->addItem(QStringLiteral("Aperte, senza separatori"), QStringLiteral("open"));
    m_rows->addItem(QStringLiteral("Filo che sfuma ai lati"), QStringLiteral("line"));
    m_rows->addItem(QStringLiteral("Strisce alternate (Dolphin originale)"), QStringLiteral("stripe"));
    m_rows->addItem(QStringLiteral("Nessuno sfondo"), QStringLiteral("none"));
    form->addRow(QStringLiteral("Sfondo delle righe:"), m_rows);

    m_icons = new QCheckBox(QStringLiteral("Icona classica all'inizio della riga, invece del segno del tipo"), this);
    form->addRow(QString(), m_icons);

    m_mark = new QComboBox(this);
    m_mark->addItem(QStringLiteral("Filigrana: icona tenue e sigla grande"), QString());
    m_mark->addItem(QStringLiteral("Tessera: disegno e sigla in un quadrato"), QStringLiteral("tile"));
    m_mark->addItem(QStringLiteral("Pillola: disegno e sigla in un'etichetta"), QStringLiteral("pill"));
    m_mark->addItem(QStringLiteral("Solo segni: disegno e sigla senza contenitore"), QStringLiteral("signs"));
    m_mark->addItem(QStringLiteral("Testata: disegno e sigla nel bordo colorato"), QStringLiteral("header"));
    form->addRow(QStringLiteral("Segno del tipo di file:"), m_mark);

    m_sigla = new QComboBox(this);
    m_sigla->addItem(QStringLiteral("Piena e tenue"), QStringLiteral("fill"));
    m_sigla->addItem(QStringLiteral("Solo contorno"), QStringLiteral("outline"));
    m_sigla->addItem(QStringLiteral("Etichetta piccola"), QStringLiteral("label"));
    form->addRow(QStringLiteral("Sigla della filigrana:"), m_sigla);

    m_ago = new QCheckBox(QStringLiteral("Sotto la data, i giorni trascorsi"), this);
    form->addRow(QString(), m_ago);
    m_rowDates = new QCheckBox(QStringLiteral("Sotto il nome: «Creato … (giorni fa) · Modificato … (giorni fa)»"), this);
    form->addRow(QString(), m_rowDates);

    m_folderSizes = new QCheckBox(QStringLiteral("Cartelle: spazio occupato dal contenuto e numero di elementi"), this);
    m_folderSizes->setToolTip(QStringLiteral("Misura il contenuto delle cartelle (impostazione di Dolphin «Dimensione delle cartelle: dimensione del contenuto», con il suo limite di profondità). Altrimenti solo il numero di elementi."));
    form->addRow(QString(), m_folderSizes);

    m_hoverPreview = new QCheckBox(QStringLiteral("Apri l'anteprima quando il mouse si ferma sull'occhio della riga"), this);
    form->addRow(QString(), m_hoverPreview);
    m_hoverDelay = new QSpinBox(this);
    m_hoverDelay->setRange(100, 5000);
    m_hoverDelay->setSingleStep(100);
    m_hoverDelay->setSuffix(QStringLiteral(" ms"));
    form->addRow(QStringLiteral("Dopo:"), m_hoverDelay);
    m_float = new QCheckBox(QStringLiteral("La scheda di anteprima galleggia sopra la finestra (può uscire dal riquadro)"), this);
    form->addRow(QString(), m_float);

    // ---- Vista a icone ----------------------------------------------------------------------------------
    QFormLayout *iconsForm = nullptr;
    group(QStringLiteral("Vista a icone"), layout, &iconsForm);
    m_tiles = new QCheckBox(QStringLiteral("Tessere scure con anteprime arrotondate"), this);
    iconsForm->addRow(QString(), m_tiles);

    // ---- Vista a card -----------------------------------------------------------------------------------
    QFormLayout *cardsForm = nullptr;
    group(QStringLiteral("Vista a card"), layout, &cardsForm);
    m_cards = new QComboBox(this);
    m_cards->addItem(QStringLiteral("A · Tessera: anteprima in alto, scheda sotto"), QStringLiteral("a"));
    m_cards->addItem(QStringLiteral("B · Scheda: icona a sinistra, tutto in tre righe"), QStringLiteral("b"));
    m_cards->addItem(QStringLiteral("C · Dossier: la riga ripiegata, giorni trascorsi a destra"), QStringLiteral("c"));
    cardsForm->addRow(QStringLiteral("Tipo di card:"), m_cards);
    m_cardsFit = new QComboBox(this);
    m_cardsFit->addItem(QStringLiteral("Card di misura fissa, spazio distribuito fra le colonne"), QString());
    m_cardsFit->addItem(QStringLiteral("Le card si allargano fino a riempire la riga"), QStringLiteral("stretch"));
    cardsForm->addRow(QStringLiteral("Griglia:"), m_cardsFit);
    m_cardsInfo = new QComboBox(this);
    m_cardsInfo->addItem(QStringLiteral("Creato e modificato"), QString());
    m_cardsInfo->addItem(QStringLiteral("Anche tipo e permessi"), QStringLiteral("full"));
    cardsForm->addRow(QStringLiteral("Dettagli nella card:"), m_cardsInfo);
    m_cardsMark = new QComboBox(this);
    m_cardsMark->addItem(QStringLiteral("L'icona del tipo"), QString());
    m_cardsMark->addItem(QStringLiteral("Il tipo come sfondo: colore e sigla in filigrana (le immagini tengono l'anteprima)"), QStringLiteral("type"));
    cardsForm->addRow(QStringLiteral("Segno del tipo:"), m_cardsMark);
    auto *cardsNote = new QLabel(QStringLiteral("La vista si attiva dal menu Visualizza (Card, Ctrl+4) o dal pulsante «Card» nella barra dei filtri; lo zoom cambia la misura delle card."), this);
    cardsNote->setWordWrap(true);
    cardsNote->setEnabled(false);
    cardsForm->addRow(QString(), cardsNote);

    auto *note = new QLabel(QStringLiteral("Le modifiche valgono appena applicate. Una variabile DOLPHIN_NG_… impostata all'avvio ha la precedenza: la voce corrispondente è disattivata."), this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);
    layout->addStretch();

    loadSettings();

    // what the environment decides cannot be changed here
    const struct {
        QWidget *widget;
        const char *name;
    } bound[] = {{m_glassPanel, "SIDEBAR"}, {m_backdrop, "GLASS_MODE"}, {m_image, "GLASS_IMAGE"},   {m_glass, "GLASS"},      {m_blur, "GLASS_BLUR"},
                 {m_motion, "GLASS_MOTION"}, {m_lavaStyle, "LAVA_STYLE"}, {m_lavaSize, "LAVA_SIZE"}, {m_lavaColours, "LAVA_COLORS"}, {m_lavaSpeed, "LAVA_SPEED"},
                 {m_rim, "GLASS_RIM"},      {m_devices, "DEVICES"},     {m_twoLines, "LIST"},        {m_rows, "ROWS"},        {m_icons, "ICONS"},
                 {m_mark, "MARK"},          {m_sigla, "SIGLA"},         {m_ago, "DATE_AGO"},         {m_tiles, "TILES"},          {m_rowDates, "ROW_DATES"},
                 {m_hoverPreview, "PREVIEW_HOVER"}, {m_hoverDelay, "PREVIEW_HOVER"}, {m_float, "PREVIEW_FLOAT"}, {m_cards, "CARDS"}, {m_cardsFit, "CARDS_FIT"}, {m_cardsInfo, "CARDS_INFO"}, {m_cardsMark, "CARDS_MARK"}};
    for (const auto &entry : bound) {
        entry.widget->setEnabled(!ngSettingFromEnvironment(entry.name));
    }

    for (QCheckBox *box : {m_glassPanel, m_motion, m_devices, m_twoLines, m_icons, m_ago, m_tiles, m_hoverPreview, m_folderSizes, m_rowDates, m_float}) {
        connect(box, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    }
    connect(m_hoverDelay, &QSpinBox::valueChanged, this, &NgSettingsPage::changed);
    for (QComboBox *combo : {m_backdrop, m_lavaStyle, m_lavaColours, m_rim, m_rows, m_mark, m_sigla, m_cards, m_cardsFit, m_cardsInfo, m_cardsMark}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    }
    for (QSlider *slider : {m_glass, m_blur, m_lavaSize, m_lavaSpeed}) {
        connect(slider, &QSlider::valueChanged, this, &NgSettingsPage::changed);
    }
    connect(m_image, &KUrlRequester::textChanged, this, &NgSettingsPage::changed);

    for (QCheckBox *box : {m_glassPanel, m_motion, m_icons, m_hoverPreview}) {
        connect(box, &QCheckBox::toggled, this, &NgSettingsPage::updateDependencies);
    }
    for (QComboBox *combo : {m_backdrop, m_lavaColours, m_mark}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &NgSettingsPage::updateDependencies);
    }
    updateDependencies();
}

NgSettingsPage::~NgSettingsPage() = default;

void NgSettingsPage::updateDependencies()
{
    const bool glass = m_glassPanel->isChecked();
    const QString mode = m_backdrop->currentData().toString();
    const bool picture = glass && mode.isEmpty();
    const bool gradient = glass && mode == QLatin1String("lava");

    m_sidebarForm->setRowVisible(m_backdrop, glass);
    m_sidebarForm->setRowVisible(m_image, picture);
    m_sidebarForm->setRowVisible(m_glass, picture);
    m_sidebarForm->setRowVisible(m_blur, picture);
    m_sidebarForm->setRowVisible(m_motion, picture);
    m_sidebarForm->setRowVisible(m_lavaStyle, gradient);
    m_sidebarForm->setRowVisible(m_lavaSize, gradient);
    m_sidebarForm->setRowVisible(m_lavaColours, gradient);
    m_sidebarForm->setRowVisible(m_lavaColourRow, gradient && m_lavaColours->currentIndex() == 2);
    m_sidebarForm->setRowVisible(m_lavaSpeed, gradient || (picture && m_motion->isChecked()));
    m_sidebarForm->setRowVisible(m_rim, glass);
    m_sidebarForm->setRowVisible(m_devices, glass);

    const bool marks = !m_icons->isChecked();
    m_listForm->setRowVisible(m_mark, marks);
    m_listForm->setRowVisible(m_sigla, marks && m_mark->currentIndex() == 0);
    m_listForm->setRowVisible(m_hoverDelay, m_hoverPreview->isChecked());
}

void NgSettingsPage::loadSettings()
{
    m_glassPanel->setChecked(ngSetting("SIDEBAR").toLower() != QLatin1String("plain"));
    m_backdrop->setCurrentIndex(qMax(0, m_backdrop->findData(ngSetting("GLASS_MODE").toLower())));
    m_image->setText(ngSetting("GLASS_IMAGE"));
    m_glass->setValue(qBound(0, ngSettingInt("GLASS", 55), 100));
    m_blur->setValue(qBound(0, ngSettingInt("GLASS_BLUR", 45), 100));
    m_motion->setChecked(ngSetting("GLASS_MOTION").toLower() == QLatin1String("drift"));
    m_lavaStyle->setCurrentIndex(ngSetting("LAVA_STYLE").toLower() == QLatin1String("blobs") ? 1 : 0);
    m_lavaSize->setValue(qBound(10, ngSettingInt("LAVA_SIZE", 55), 100));
    m_lavaSpeed->setValue(qBound(5, ngSettingInt("LAVA_SPEED", 30), 100));
    static const char *const defaults[3] = {"#2f6f8f", "#8a5a2c", "#3c4a78"};
    const QString spec = ngSetting("LAVA_COLORS").trimmed().toLower();
    const QStringList custom = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool isCustom = !spec.isEmpty() && spec != QLatin1String("wallpaper");
    for (int i = 0; i < 3; ++i) {
        const QColor colour(isCustom && i < custom.size() ? custom.at(i).trimmed() : QString::fromLatin1(defaults[i]));
        m_lavaColour[i]->setColor(colour.isValid() ? colour : QColor(QString::fromLatin1(defaults[i])));
    }
    m_lavaColours->setCurrentIndex(spec.isEmpty() ? 0 : spec == QLatin1String("wallpaper") ? 1 : 2);
    {
        const QString rim = ngSetting("GLASS_RIM").toLower();
        m_rim->setCurrentIndex(qMax(0, m_rim->findData(rim == QLatin1String("show") ? QStringLiteral("diagonal") : rim)));
    }
    m_devices->setChecked(ngSetting("DEVICES").toLower() == QLatin1String("show"));

    m_twoLines->setChecked(ngSetting("LIST").toLower() != QLatin1String("columns"));
    static const NgRowStyle order[] = {NgRowStyle::Card, NgRowStyle::Open, NgRowStyle::Line, NgRowStyle::Stripe, NgRowStyle::None};
    for (int i = 0; i < 5; ++i) {
        if (order[i] == ngRowStyle()) {
            m_rows->setCurrentIndex(i);
        }
    }
    m_icons->setChecked(ngSetting("ICONS").toLower() == QLatin1String("show"));
    m_mark->setCurrentIndex(qMax(0, m_mark->findData(ngSetting("MARK").toLower())));
    m_sigla->setCurrentIndex(qMax(0, m_sigla->findData(ngSetting("SIGLA", QStringLiteral("fill")).toLower())));
    m_ago->setChecked(ngSetting("DATE_AGO").toLower() != QLatin1String("hide"));
    m_rowDates->setChecked(ngSetting("ROW_DATES").toLower() != QLatin1String("hide"));
    m_folderSizes->setChecked(ContentDisplaySettings::directorySizeMode() == ContentDisplaySettings::EnumDirectorySizeMode::ContentSize);
    const int hover = ngSettingInt("PREVIEW_HOVER", 0);
    m_hoverPreview->setChecked(hover > 0);
    m_hoverDelay->setValue(hover > 0 ? qBound(100, hover, 5000) : 700);
    m_float->setChecked(ngSetting("PREVIEW_FLOAT") == QLatin1String("1"));
    m_tiles->setChecked(ngSetting("TILES").toLower() != QLatin1String("off"));
    static const char *const looks[] = {"a", "b", "c"};
    m_cards->setCurrentIndex(int(ngCardStyle()));
    Q_UNUSED(looks)
    m_cardsFit->setCurrentIndex(ngCardsStretch() ? 1 : 0);
    m_cardsInfo->setCurrentIndex(ngSetting("CARDS_INFO").toLower() == QLatin1String("full") ? 1 : 0);
    m_cardsMark->setCurrentIndex(ngSetting("CARDS_MARK").toLower() == QLatin1String("type") ? 1 : 0);
}

void NgSettingsPage::applySettings()
{
    setNgSetting("SIDEBAR", m_glassPanel->isChecked() ? QString() : QStringLiteral("plain"));
    setNgSetting("GLASS_MODE", m_backdrop->currentData().toString());
    setNgSetting("GLASS_IMAGE", m_image->text().trimmed());
    setNgSetting("GLASS", QString::number(m_glass->value()));
    setNgSetting("GLASS_BLUR", QString::number(m_blur->value()));
    setNgSetting("GLASS_MOTION", m_motion->isChecked() ? QStringLiteral("drift") : QString());
    setNgSetting("LAVA_STYLE", m_lavaStyle->currentData().toString());
    setNgSetting("LAVA_SIZE", QString::number(m_lavaSize->value()));
    setNgSetting("LAVA_SPEED", QString::number(m_lavaSpeed->value()));
    const int colours = m_lavaColours->currentIndex();
    setNgSetting("LAVA_COLORS",
                 colours == 0   ? QString()
                 : colours == 1 ? QStringLiteral("wallpaper")
                                : QStringLiteral("%1,%2,%3").arg(m_lavaColour[0]->color().name(), m_lavaColour[1]->color().name(), m_lavaColour[2]->color().name()));
    setNgSetting("GLASS_RIM", m_rim->currentData().toString());
    setNgSetting("DEVICES", m_devices->isChecked() ? QStringLiteral("show") : QString());

    setNgSetting("LIST", m_twoLines->isChecked() ? QString() : QStringLiteral("columns"));
    if (m_rows->isEnabled()) {
        static const NgRowStyle order[] = {NgRowStyle::Card, NgRowStyle::Open, NgRowStyle::Line, NgRowStyle::Stripe, NgRowStyle::None};
        setNgRowStyle(order[qBound(0, m_rows->currentIndex(), 4)]);
    }
    setNgSetting("ICONS", m_icons->isChecked() ? QStringLiteral("show") : QString());
    setNgSetting("MARK", m_mark->currentData().toString());
    setNgSetting("SIGLA", m_sigla->currentData().toString());
    setNgSetting("DATE_AGO", m_ago->isChecked() ? QString() : QStringLiteral("hide"));
    setNgSetting("ROW_DATES", m_rowDates->isChecked() ? QString() : QStringLiteral("hide"));
    if (m_folderSizes->isChecked() != (ContentDisplaySettings::directorySizeMode() == ContentDisplaySettings::EnumDirectorySizeMode::ContentSize)) {
        ContentDisplaySettings::setDirectorySizeMode(m_folderSizes->isChecked() ? ContentDisplaySettings::EnumDirectorySizeMode::ContentSize
                                                                                : ContentDisplaySettings::EnumDirectorySizeMode::ContentCount);
        ContentDisplaySettings::self()->save();
    }
    setNgSetting("PREVIEW_FLOAT", m_float->isChecked() ? QStringLiteral("1") : QString());
    setNgSetting("PREVIEW_HOVER", m_hoverPreview->isChecked() ? QString::number(m_hoverDelay->value()) : QString());
    setNgSetting("TILES", m_tiles->isChecked() ? QString() : QStringLiteral("off"));
    setNgSetting("CARDS", m_cards->currentData().toString());
    setNgSetting("CARDS_FIT", m_cardsFit->currentData().toString());
    setNgSetting("CARDS_INFO", m_cardsInfo->currentData().toString());
    setNgSetting("CARDS_MARK", m_cardsMark->currentData().toString());

    ngSettingsInvalidate(); // everything that depends on these re-reads them now
}

void NgSettingsPage::restoreDefaults()
{
    m_glassPanel->setChecked(true);
    m_backdrop->setCurrentIndex(0);
    m_image->clear();
    m_glass->setValue(55);
    m_blur->setValue(45);
    m_motion->setChecked(false);
    m_lavaStyle->setCurrentIndex(0);
    m_lavaSize->setValue(55);
    m_lavaSpeed->setValue(30);
    m_lavaColours->setCurrentIndex(0);
    m_rim->setCurrentIndex(0);
    m_devices->setChecked(false);
    m_twoLines->setChecked(true);
    m_rows->setCurrentIndex(0);
    m_icons->setChecked(false);
    m_mark->setCurrentIndex(0);
    m_sigla->setCurrentIndex(0);
    m_ago->setChecked(true);
    m_rowDates->setChecked(true);
    m_hoverPreview->setChecked(false);
    m_hoverDelay->setValue(700);
    m_float->setChecked(false);
    m_folderSizes->setChecked(false);
    m_tiles->setChecked(true);
}

#include "moc_ngsettingspage.cpp"

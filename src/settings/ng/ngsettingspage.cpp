/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ngsettingspage.h"

#include "kitemviews/kitemlistwidget.h"
#include "dolphin_contentdisplaysettings.h"

#include <KColorButton>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <KUrlRequester>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
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
// The blur behind the live sidebar is the compositor's: Better Blur keeps its strength (1-15) in
// kwinrc [Effect-blurplus], KWin's own blur in [Effect-blur].
KConfigGroup kwinBlurGroup()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    return config->group(config->hasGroup(QStringLiteral("Effect-blurplus")) ? QStringLiteral("Effect-blurplus") : QStringLiteral("Effect-blur"));
}
void reloadKWinBlur()
{
    const QString kwin = QStringLiteral("org.kde.KWin"), path = QStringLiteral("/Effects"), iface = QStringLiteral("org.kde.kwin.Effects");
    const QStringList effects = {QStringLiteral("forceblur_x11"), QStringLiteral("forceblur"), QStringLiteral("blur")};
    for (const QString &effect : effects) {
        QDBusMessage call = QDBusMessage::createMethodCall(kwin, path, iface, QStringLiteral("reconfigureEffect"));
        call.setArguments({effect});
        QDBusConnection::sessionBus().asyncCall(call);
    }
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
    // the page is long: it scrolls instead of squeezing its rows into the height of the dialog
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    auto *content = new QWidget(scroll);
    content->setAutoFillBackground(false);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);

    // ---- Preset: a whole look in one choice, for those who would rather not go through every option --------
    {
        QFormLayout *presetForm = nullptr;
        group(ngTr("Look", "Aspetto"), layout, &presetForm);
        m_preset = new QComboBox(this);
        m_preset->addItem(ngTr("Pick a preset…", "Scegli un preset…"), QString());
        m_preset->addItem(ngTr("Classic – like Dolphin: plain panel, columns, icons", "Classico – come Dolphin: pannello normale, colonne, icone"), QStringLiteral("classic"));
        m_preset->addItem(ngTr("Soft – NG list and cards, panel without glass", "Soft – lista e card NG, pannello senza vetro"), QStringLiteral("soft"));
        m_preset->addItem(ngTr("Glass – everything: glass panel, NG list and cards", "Glass – tutto: pannello a vetro, lista e card NG"), QStringLiteral("glass"));
        presetForm->addRow(QStringLiteral("Preset:"), m_preset);
        auto *presetNote = new QLabel(ngTr("A preset fills in the options below; you can adjust them before applying.", "Un preset compila le opzioni qui sotto; puoi ritoccarle prima di applicare."), this);
        presetNote->setWordWrap(true);
        presetNote->setEnabled(false);
        presetForm->addRow(QString(), presetNote);
        connect(m_preset, &QComboBox::activated, this, [this](int index) {
            const QString name = m_preset->itemData(index).toString();
            if (!name.isEmpty()) {
                applyPreset(name);
                Q_EMIT changed();
            }
        });
    }

    // ---- Barra laterale ---------------------------------------------------------------------------------
    group(ngTr("Sidebar", "Barra laterale"), layout, &m_sidebarForm);
    QFormLayout *form = m_sidebarForm;

    m_glassPanel = new QCheckBox(ngTr("Glass panel with a backdrop", "Pannello a vetro con sfondo"), this);
    form->addRow(QString(), m_glassPanel);

    m_backdrop = new QComboBox(this);
    m_backdrop->addItem(ngTr("Blurred image", "Immagine sfocata"), QString());
    m_backdrop->addItem(ngTr("Animated gradient", "Gradiente animato"), QStringLiteral("lava"));
    m_backdrop->addItem(ngTr("Real desktop blur (compositor)", "Blur vero del desktop (compositor)"), QStringLiteral("live"));
    form->addRow(ngTr("Backdrop:", "Sfondo:"), m_backdrop);

    // picture
    m_image = new KUrlRequester(this);
    m_image->setMode(KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
    m_image->setPlaceholderText(ngTr("Desktop wallpaper", "Sfondo del desktop"));
    m_image->setToolTip(ngTr("Image shown, blurred, behind the sidebar. Empty: the wallpaper of each screen.", "Immagine mostrata, sfocata, dietro la barra laterale. Vuoto: lo sfondo del desktop di ogni schermo."));
    form->addRow(ngTr("Image:", "Immagine:"), m_image);
    m_glass = percentSlider(this, 0, 100);
    form->addRow(ngTr("How much shows:", "Quanto si vede:"), m_glass);
    m_blur = percentSlider(this, 0, 100);
    form->addRow(ngTr("Blur:", "Sfocatura:"), m_blur);
    m_liveBlur = percentSlider(this, 1, 15);
    m_liveBlur->setPageStep(1);
    m_liveBlur->setToolTip(ngTr("The blur strength of the compositor (KWin's Blur or Better Blur effect). It is a desktop setting: every blurred window changes with it.",
                                "L'intensità del blur del compositor (effetto Sfocatura di KWin o Better Blur). È un'impostazione del desktop: cambia per tutte le finestre sfocate."));
    form->addRow(ngTr("Blur (whole desktop):", "Sfocatura (tutto il desktop):"), m_liveBlur);
    m_motion = new QCheckBox(ngTr("The image moves slowly (zoom and pan)", "L'immagine si muove lentamente (zoom e scorrimento)"), this);
    form->addRow(QString(), m_motion);

    // animated gradient
    m_lavaStyle = new QComboBox(this);
    m_lavaStyle->addItem(ngTr("Flowing gradient", "Gradiente che scorre"), QString());
    m_lavaStyle->addItem(ngTr("Blobs (lava lamp)", "Macchie (lava lamp)"), QStringLiteral("blobs"));
    form->addRow(ngTr("Animation:", "Animazione:"), m_lavaStyle);
    m_lavaSize = percentSlider(this, 10, 100);
    form->addRow(ngTr("Size of the colour areas:", "Ampiezza delle zone di colore:"), m_lavaSize);
    m_lavaColours = new QComboBox(this);
    m_lavaColours->addItem(ngTr("Teal, amber and indigo", "Petrolio, ambra e indaco"), QString());
    m_lavaColours->addItem(ngTr("From the desktop wallpaper", "Dallo sfondo del desktop"), QStringLiteral("wallpaper"));
    m_lavaColours->addItem(ngTr("Custom", "Personalizzati"), QStringLiteral("custom"));
    form->addRow(ngTr("Colours:", "Colori:"), m_lavaColours);
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
    form->addRow(ngTr("Speed of the motion:", "Velocità del movimento:"), m_lavaSpeed);

    m_rim = new QComboBox(this);
    m_rim->addItem(ngTr("None", "Nessuno"), QString());
    m_rim->addItem(ngTr("Diagonal highlight", "Riflesso in diagonale"), QStringLiteral("diagonal"));
    m_rim->addItem(ngTr("Thin even line", "Linea sottile uniforme"), QStringLiteral("soft"));
    m_rim->addItem(ngTr("Bevel (dark outside, light inside)", "Smusso (scuro fuori, chiaro dentro)"), QStringLiteral("bevel"));
    m_rim->addItem(ngTr("Soft glow", "Alone sfumato"), QStringLiteral("glow"));
    m_rim->addItem(ngTr("Light from above", "Luce dall'alto"), QStringLiteral("top"));
    form->addRow(ngTr("Panel edge:", "Bordo del pannello:"), m_rim);

    m_devices = new QCheckBox(ngTr("Show the “Devices” section (otherwise the disk box is shown)", "Mostra la sezione «Dispositivi» (altrimenti c'è il riquadro del disco)"), this);
    form->addRow(QString(), m_devices);
    m_sidebarFilters = new QCheckBox(ngTr("Show the “Filters” section at the bottom of the panel", "Mostra la sezione «Filtri» in fondo al pannello"), this);
    form->addRow(QString(), m_sidebarFilters);

    // ---- Elenco dei file --------------------------------------------------------------------------------
    group(ngTr("File list (details view)", "Elenco dei file (vista dettagli)"), layout, &m_listForm);
    form = m_listForm;

    m_twoLines = new QCheckBox(ngTr("Two-line rows, without columns", "Righe su due linee, senza colonne"), this);
    form->addRow(QString(), m_twoLines);

    m_rows = new QComboBox(this);
    m_rows->addItem(ngTr("Cards", "Schede"), QStringLiteral("card"));
    m_rows->addItem(ngTr("Open, without separators", "Aperte, senza separatori"), QStringLiteral("open"));
    m_rows->addItem(ngTr("Line fading at the sides", "Filo che sfuma ai lati"), QStringLiteral("line"));
    m_rows->addItem(ngTr("Alternating stripes (stock Dolphin)", "Strisce alternate (Dolphin originale)"), QStringLiteral("stripe"));
    m_rows->addItem(ngTr("No background", "Nessuno sfondo"), QStringLiteral("none"));
    form->addRow(ngTr("Row background:", "Sfondo delle righe:"), m_rows);

    m_icons = new QCheckBox(ngTr("Classic icon at the start of the row, instead of the type mark", "Icona classica all'inizio della riga, invece del segno del tipo"), this);
    form->addRow(QString(), m_icons);

    m_mark = new QComboBox(this);
    m_mark->addItem(ngTr("Watermark: faint icon and large letters", "Filigrana: icona tenue e sigla grande"), QString());
    m_mark->addItem(ngTr("Tile: glyph and letters in a square", "Tessera: disegno e sigla in un quadrato"), QStringLiteral("tile"));
    m_mark->addItem(ngTr("Pill: glyph and letters in a label", "Pillola: disegno e sigla in un'etichetta"), QStringLiteral("pill"));
    m_mark->addItem(ngTr("Marks only: glyph and letters without a container", "Solo segni: disegno e sigla senza contenitore"), QStringLiteral("signs"));
    m_mark->addItem(ngTr("Header: glyph and letters in the coloured edge", "Testata: disegno e sigla nel bordo colorato"), QStringLiteral("header"));
    form->addRow(ngTr("File type mark:", "Segno del tipo di file:"), m_mark);

    m_sigla = new QComboBox(this);
    m_sigla->addItem(ngTr("Filled and faint", "Piena e tenue"), QStringLiteral("fill"));
    m_sigla->addItem(ngTr("Outline only", "Solo contorno"), QStringLiteral("outline"));
    m_sigla->addItem(ngTr("Small label", "Etichetta piccola"), QStringLiteral("label"));
    form->addRow(ngTr("Watermark letters:", "Sigla della filigrana:"), m_sigla);

    m_ago = new QCheckBox(ngTr("Under the date, the days elapsed", "Sotto la data, i giorni trascorsi"), this);
    form->addRow(QString(), m_ago);
    m_rowDates = new QCheckBox(ngTr("Under the name: “Created … (days ago) · Modified … (days ago)”", "Sotto il nome: «Creato … (giorni fa) · Modificato … (giorni fa)»"), this);
    form->addRow(QString(), m_rowDates);

    m_folderSizes = new QCheckBox(ngTr("Folders: space used by the contents and number of items", "Cartelle: spazio occupato dal contenuto e numero di elementi"), this);
    m_folderSizes->setToolTip(ngTr("Measures the contents of folders (Dolphin's setting “Folder size: size of contents”, with its depth limit). Otherwise only the number of items.", "Misura il contenuto delle cartelle (impostazione di Dolphin «Dimensione delle cartelle: dimensione del contenuto», con il suo limite di profondità). Altrimenti solo il numero di elementi."));
    form->addRow(QString(), m_folderSizes);

    m_hoverPreview = new QCheckBox(ngTr("Open the preview when the mouse rests on the eye of the row", "Apri l'anteprima quando il mouse si ferma sull'occhio della riga"), this);
    form->addRow(QString(), m_hoverPreview);
    m_hoverDelay = new QSpinBox(this);
    m_hoverDelay->setRange(100, 5000);
    m_hoverDelay->setSingleStep(100);
    m_hoverDelay->setSuffix(QStringLiteral(" ms"));
    form->addRow(ngTr("After:", "Dopo:"), m_hoverDelay);
    m_float = new QCheckBox(ngTr("The preview card floats above the window (it can leave the pane)", "La scheda di anteprima galleggia sopra la finestra (può uscire dal riquadro)"), this);
    form->addRow(QString(), m_float);

    // ---- Vista a icone ----------------------------------------------------------------------------------
    QFormLayout *iconsForm = nullptr;
    group(ngTr("Icons view", "Vista a icone"), layout, &iconsForm);
    m_tiles = new QCheckBox(ngTr("Dark tiles with rounded previews", "Tessere scure con anteprime arrotondate"), this);
    iconsForm->addRow(QString(), m_tiles);

    // ---- Vista a card -----------------------------------------------------------------------------------
    QFormLayout *cardsForm = nullptr;
    group(ngTr("Cards view", "Vista a card"), layout, &cardsForm);
    m_cards = new QComboBox(this);
    m_cards->addItem(ngTr("A · Tessera: preview on top, details below", "A · Tessera: anteprima in alto, scheda sotto"), QStringLiteral("a"));
    m_cards->addItem(ngTr("B · Scheda: icon on the left, everything in three lines", "B · Scheda: icona a sinistra, tutto in tre righe"), QStringLiteral("b"));
    m_cards->addItem(ngTr("C · Dossier: the folded row, days elapsed on the right", "C · Dossier: la riga ripiegata, giorni trascorsi a destra"), QStringLiteral("c"));
    cardsForm->addRow(ngTr("Kind of card:", "Tipo di card:"), m_cards);
    m_cardsFit = new QComboBox(this);
    m_cardsFit->addItem(ngTr("Fixed-size cards, space shared between the columns", "Card di misura fissa, spazio distribuito fra le colonne"), QString());
    m_cardsFit->addItem(ngTr("Cards stretch to fill the row", "Le card si allargano fino a riempire la riga"), QStringLiteral("stretch"));
    cardsForm->addRow(ngTr("Grid:", "Griglia:"), m_cardsFit);
    m_cardsInfo = new QComboBox(this);
    m_cardsInfo->addItem(ngTr("Created and modified", "Creato e modificato"), QString());
    m_cardsInfo->addItem(ngTr("Type and permissions too", "Anche tipo e permessi"), QStringLiteral("full"));
    cardsForm->addRow(ngTr("Details in the card:", "Dettagli nella card:"), m_cardsInfo);
    m_cardsMark = new QComboBox(this);
    m_cardsMark->addItem(ngTr("The type icon", "L'icona del tipo"), QString());
    m_cardsMark->addItem(ngTr("The type as background: colour and watermark letters (images keep their preview)", "Il tipo come sfondo: colore e sigla in filigrana (le immagini tengono l'anteprima)"), QStringLiteral("type"));
    cardsForm->addRow(ngTr("Type mark:", "Segno del tipo:"), m_cardsMark);
    m_quickView = new QCheckBox(ngTr("Quick view when hovering the picture of a card (images, GIFs, videos, PDFs)", "Anteprima al volo passando sull'immagine della card (immagini, GIF, video, PDF)"), this);
    cardsForm->addRow(QString(), m_quickView);
    m_quickDelay = new QSpinBox(this);
    m_quickDelay->setRange(0, 3000);
    m_quickDelay->setSingleStep(50);
    m_quickDelay->setSuffix(QStringLiteral(" ms"));
    m_quickDelay->setSpecialValueText(ngTr("At once", "Subito"));
    cardsForm->addRow(ngTr("After:", "Dopo:"), m_quickDelay);
    m_quickSide = new QComboBox(this);
    m_quickSide->addItem(ngTr("Left of the cursor", "A sinistra del cursore"), QString());
    m_quickSide->addItem(ngTr("Right of the cursor", "A destra del cursore"), QStringLiteral("right"));
    m_quickSide->addItem(ngTr("Automatic, next to the card", "Automatica, accanto alla card"), QStringLiteral("auto"));
    cardsForm->addRow(ngTr("Position:", "Posizione:"), m_quickSide);
    connect(m_quickSide, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_quickView, &QCheckBox::toggled, m_quickSide, &QWidget::setEnabled);
    connect(m_quickView, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    connect(m_quickView, &QCheckBox::toggled, m_quickDelay, &QWidget::setEnabled);
    connect(m_quickDelay, &QSpinBox::valueChanged, this, &NgSettingsPage::changed);
    auto *cardsNote = new QLabel(ngTr("Switch to the view from the View menu (Cards, Ctrl+4) or with the “Cards” button in the filter bar; zooming changes the size of the cards.", "La vista si attiva dal menu Visualizza (Card, Ctrl+4) o dal pulsante «Card» nella barra dei filtri; lo zoom cambia la misura delle card."), this);
    cardsNote->setWordWrap(true);
    cardsNote->setEnabled(false);
    cardsForm->addRow(QString(), cardsNote);

    auto *note = new QLabel(ngTr("Changes take effect as soon as they are applied. A DOLPHIN_NG_… variable set at start-up wins: the matching entry is disabled.", "Le modifiche valgono appena applicate. Una variabile DOLPHIN_NG_… impostata all'avvio ha la precedenza: la voce corrispondente è disattivata."), this);
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
                 {m_hoverPreview, "PREVIEW_HOVER"}, {m_hoverDelay, "PREVIEW_HOVER"}, {m_float, "PREVIEW_FLOAT"}, {m_cards, "CARDS"}, {m_cardsFit, "CARDS_FIT"}, {m_cardsInfo, "CARDS_INFO"}, {m_cardsMark, "CARDS_MARK"}, {m_sidebarFilters, "SIDEBAR_FILTERS"}};
    for (const auto &entry : bound) {
        entry.widget->setEnabled(!ngSettingFromEnvironment(entry.name));
    }

    for (QCheckBox *box : {m_glassPanel, m_motion, m_devices, m_twoLines, m_icons, m_ago, m_tiles, m_hoverPreview, m_folderSizes, m_rowDates, m_float, m_sidebarFilters}) {
        connect(box, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    }
    connect(m_hoverDelay, &QSpinBox::valueChanged, this, &NgSettingsPage::changed);
    for (QComboBox *combo : {m_backdrop, m_lavaStyle, m_lavaColours, m_rim, m_rows, m_mark, m_sigla, m_cards, m_cardsFit, m_cardsInfo, m_cardsMark}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    }
    for (QSlider *slider : {m_glass, m_blur, m_liveBlur, m_lavaSize, m_lavaSpeed}) {
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
    m_sidebarForm->setRowVisible(m_liveBlur, glass && mode == QLatin1String("live"));
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
    m_liveBlur->setValue(qBound(1, kwinBlurGroup().readEntry("BlurStrength", 15), 15));
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
    m_sidebarFilters->setChecked(ngSetting("SIDEBAR_FILTERS").toLower() != QLatin1String("hide"));

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
    const QString quick = ngSetting("CARDS_QUICKVIEW").toLower();
    m_quickView->setChecked(quick != QLatin1String("off"));
    m_quickDelay->setValue(qBound(0, quick.toInt(), 3000));
    m_quickDelay->setEnabled(m_quickView->isChecked());
    m_quickSide->setCurrentIndex(qMax(0, m_quickSide->findData(ngSetting("CARDS_QUICKVIEW_SIDE").toLower())));
    m_quickSide->setEnabled(m_quickView->isChecked());
}

void NgSettingsPage::applySettings()
{
    setNgSetting("SIDEBAR", m_glassPanel->isChecked() ? QString() : QStringLiteral("plain"));
    setNgSetting("GLASS_MODE", m_backdrop->currentData().toString());
    setNgSetting("GLASS_IMAGE", m_image->text().trimmed());
    setNgSetting("GLASS", QString::number(m_glass->value()));
    setNgSetting("GLASS_BLUR", QString::number(m_blur->value()));
    if (m_backdrop->currentData().toString() == QLatin1String("live") && kwinBlurGroup().readEntry("BlurStrength", 15) != m_liveBlur->value()) {
        KConfigGroup blur = kwinBlurGroup();
        blur.writeEntry("BlurStrength", m_liveBlur->value());
        blur.sync();
        reloadKWinBlur();
    }
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
    setNgSetting("SIDEBAR_FILTERS", m_sidebarFilters->isChecked() ? QString() : QStringLiteral("hide"));

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
    setNgSetting("CARDS_QUICKVIEW_SIDE", m_quickSide->currentData().toString());
    setNgSetting("CARDS_QUICKVIEW", !m_quickView->isChecked() ? QStringLiteral("off") : m_quickDelay->value() > 0 ? QString::number(m_quickDelay->value()) : QString());

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
    m_sidebarFilters->setChecked(true);
    m_cards->setCurrentIndex(0);
    m_cardsFit->setCurrentIndex(0);
    m_cardsInfo->setCurrentIndex(0);
    m_cardsMark->setCurrentIndex(0);
    m_quickView->setChecked(true);
    m_quickDelay->setValue(0);
    m_quickSide->setCurrentIndex(0);
}

void NgSettingsPage::applyPreset(const QString &name)
{
    restoreDefaults(); // Glass is the look the fork was designed around: the defaults
    if (name == QLatin1String("classic")) {
        // as close to stock Dolphin as the fork gets
        m_glassPanel->setChecked(false);
        m_devices->setChecked(true);
        m_sidebarFilters->setChecked(false);
        m_twoLines->setChecked(false);
        m_rows->setCurrentIndex(3); // alternating stripes
        m_icons->setChecked(true);
        m_tiles->setChecked(false);
    } else if (name == QLatin1String("soft")) {
        m_glassPanel->setChecked(false);
        m_devices->setChecked(true);
    } else {
        m_rim->setCurrentIndex(qMax(0, m_rim->findData(QStringLiteral("diagonal"))));
        m_cardsMark->setCurrentIndex(1); // the type as the background of the cards
    }
    updateDependencies();
}

#include "moc_ngsettingspage.cpp"

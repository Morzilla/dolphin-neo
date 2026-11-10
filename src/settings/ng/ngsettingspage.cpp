/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ngsettingspage.h"

#include "kitemviews/kitemlistwidget.h"

#include <KColorButton>
#include <KUrlRequester>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGraphicsView>
#include <QLabel>
#include <QSlider>

// The labels are in Italian for now, like the other dolphin-ng strings; they should go through i18n.

NgSettingsPage::NgSettingsPage(QWidget *parent)
    : SettingsPageBase(parent)
{
    auto *form = new QFormLayout(this);
    auto heading = [this, form](const QString &text) {
        auto *label = new QLabel(text, this);
        QFont bold = label->font();
        bold.setBold(true);
        label->setFont(bold);
        label->setContentsMargins(0, 10, 0, 2);
        form->addRow(label);
    };

    heading(QStringLiteral("Barra laterale"));
    m_glassPanel = new QCheckBox(QStringLiteral("Pannello a vetro *"), this);
    form->addRow(QString(), m_glassPanel);
    m_backdrop = new QComboBox(this);
    m_backdrop->addItem(QStringLiteral("Immagine sfocata"), QString());
    m_backdrop->addItem(QStringLiteral("Gradiente animato (molto lento)"), QStringLiteral("lava"));
    m_backdrop->addItem(QStringLiteral("Blur vero del desktop *"), QStringLiteral("live"));
    form->addRow(QStringLiteral("Sfondo del pannello:"), m_backdrop);
    m_lavaStyle = new QComboBox(this);
    m_lavaStyle->addItem(QStringLiteral("Gradiente che scorre"), QString());
    m_lavaStyle->addItem(QStringLiteral("Macchie (lava lamp)"), QStringLiteral("blobs"));
    form->addRow(QStringLiteral("Tipo di animazione:"), m_lavaStyle);
    m_lavaSpeed = new QSlider(Qt::Horizontal, this);
    m_lavaSpeed->setRange(5, 100);
    form->addRow(QStringLiteral("Velocità dell'animazione:"), m_lavaSpeed);
    m_lavaSize = new QSlider(Qt::Horizontal, this);
    m_lavaSize->setRange(10, 100);
    form->addRow(QStringLiteral("Ampiezza delle zone di colore:"), m_lavaSize);
    m_lavaColours = new QComboBox(this);
    m_lavaColours->addItem(QStringLiteral("Petrolio, ambra e indaco (come nel mockup)"), QString());
    m_lavaColours->addItem(QStringLiteral("Dallo sfondo del desktop"), QStringLiteral("wallpaper"));
    m_lavaColours->addItem(QStringLiteral("Personalizzati"), QStringLiteral("custom"));
    form->addRow(QStringLiteral("Colori dell'animazione:"), m_lavaColours);
    auto *colourRow = new QHBoxLayout();
    for (KColorButton *&button : m_lavaColour) {
        button = new KColorButton(this);
        colourRow->addWidget(button);
        connect(button, &KColorButton::changed, this, &NgSettingsPage::changed);
    }
    colourRow->addStretch();
    form->addRow(QString(), colourRow);
    connect(m_lavaColours, &QComboBox::currentIndexChanged, this, [this](int index) {
        for (KColorButton *button : m_lavaColour) {
            button->setEnabled(index == 2); // only the custom set uses them
        }
    });
    m_devices = new QCheckBox(QStringLiteral("Mostra la sezione «Dispositivi» *"), this);
    form->addRow(QString(), m_devices);

    m_image = new KUrlRequester(this);
    m_image->setMode(KFile::File | KFile::ExistingOnly | KFile::LocalOnly);
    m_image->setPlaceholderText(QStringLiteral("Sfondo del desktop"));
    m_image->setToolTip(QStringLiteral("Immagine mostrata, sfocata, dietro la barra laterale. Vuoto: lo sfondo del desktop di ogni schermo."));
    form->addRow(QStringLiteral("Immagine della barra laterale:"), m_image);

    m_glass = new QSlider(Qt::Horizontal, this);
    m_glass->setRange(0, 100);
    form->addRow(QStringLiteral("Quanto si vede l'immagine:"), m_glass);

    m_blur = new QSlider(Qt::Horizontal, this);
    m_blur->setRange(0, 100);
    form->addRow(QStringLiteral("Sfocatura dell'immagine:"), m_blur);

    heading(QStringLiteral("Elenco dei file"));
    m_twoLines = new QCheckBox(QStringLiteral("Righe su due linee, senza colonne *"), this);
    form->addRow(QString(), m_twoLines);

    m_rows = new QComboBox(this);
    m_rows->addItem(QStringLiteral("Schede"), QStringLiteral("card"));
    m_rows->addItem(QStringLiteral("Aperte, separate da un filo"), QStringLiteral("open"));
    m_rows->addItem(QStringLiteral("Filo che sfuma ai lati"), QStringLiteral("line"));
    m_rows->addItem(QStringLiteral("Strisce alternate (Dolphin originale)"), QStringLiteral("stripe"));
    m_rows->addItem(QStringLiteral("Nessuno sfondo"), QStringLiteral("none"));
    form->addRow(QStringLiteral("Righe dell'elenco:"), m_rows);

    m_mark = new QComboBox(this);
    m_mark->addItem(QStringLiteral("Filigrana: icona tenue e sigla grande"), QString());
    m_mark->addItem(QStringLiteral("Tessera: disegno e sigla in un quadrato"), QStringLiteral("tile"));
    m_mark->addItem(QStringLiteral("Pillola: disegno e sigla in un'etichetta"), QStringLiteral("pill"));
    m_mark->addItem(QStringLiteral("Solo segni: disegno e sigla senza contenitore"), QStringLiteral("signs"));
    m_mark->addItem(QStringLiteral("Testata: disegno e sigla nel bordo colorato"), QStringLiteral("header"));
    form->addRow(QStringLiteral("Segno del tipo di file *:"), m_mark);

    m_sigla = new QComboBox(this);
    m_sigla->addItem(QStringLiteral("Piena e tenue"), QStringLiteral("fill"));
    m_sigla->addItem(QStringLiteral("Solo contorno"), QStringLiteral("outline"));
    m_sigla->addItem(QStringLiteral("Etichetta piccola"), QStringLiteral("label"));
    form->addRow(QStringLiteral("Sigla della filigrana *:"), m_sigla);

    m_icons = new QCheckBox(QStringLiteral("Mostra le icone nell'elenco al posto della sigla *"), this);
    form->addRow(QString(), m_icons);

    heading(QStringLiteral("Vista a icone"));
    m_tiles = new QCheckBox(QStringLiteral("Tessere scure con anteprime arrotondate *"), this);
    form->addRow(QString(), m_tiles);

    auto *note = new QLabel(QStringLiteral("* Vale dal prossimo avvio di Dolphin. Una variabile DOLPHIN_NG_… impostata all'avvio ha la precedenza: la voce corrispondente qui è disattivata."), this);
    note->setWordWrap(true);
    note->setEnabled(false);
    form->addRow(note);

    loadSettings();

    // what the environment decides cannot be changed here
    m_image->setEnabled(!ngSettingFromEnvironment("GLASS_IMAGE"));
    m_glass->setEnabled(!ngSettingFromEnvironment("GLASS"));
    m_blur->setEnabled(!ngSettingFromEnvironment("GLASS_BLUR"));
    m_rows->setEnabled(!ngSettingFromEnvironment("ROWS"));
    m_sigla->setEnabled(!ngSettingFromEnvironment("SIGLA"));
    m_mark->setEnabled(!ngSettingFromEnvironment("MARK"));
    m_icons->setEnabled(!ngSettingFromEnvironment("ICONS"));
    m_tiles->setEnabled(!ngSettingFromEnvironment("TILES"));
    m_twoLines->setEnabled(!ngSettingFromEnvironment("LIST"));
    m_glassPanel->setEnabled(!ngSettingFromEnvironment("SIDEBAR"));
    m_backdrop->setEnabled(!ngSettingFromEnvironment("GLASS_MODE"));
    m_lavaSpeed->setEnabled(!ngSettingFromEnvironment("LAVA_SPEED"));
    m_devices->setEnabled(!ngSettingFromEnvironment("DEVICES"));

    connect(m_image, &KUrlRequester::textChanged, this, &NgSettingsPage::changed);
    connect(m_glass, &QSlider::valueChanged, this, &NgSettingsPage::changed);
    connect(m_blur, &QSlider::valueChanged, this, &NgSettingsPage::changed);
    connect(m_rows, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_sigla, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_mark, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_icons, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    connect(m_tiles, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    connect(m_twoLines, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    connect(m_glassPanel, &QCheckBox::toggled, this, &NgSettingsPage::changed);
    connect(m_backdrop, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_lavaSpeed, &QSlider::valueChanged, this, &NgSettingsPage::changed);
    connect(m_lavaSize, &QSlider::valueChanged, this, &NgSettingsPage::changed);
    connect(m_lavaStyle, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_lavaColours, &QComboBox::currentIndexChanged, this, &NgSettingsPage::changed);
    connect(m_devices, &QCheckBox::toggled, this, &NgSettingsPage::changed);
}

NgSettingsPage::~NgSettingsPage() = default;

void NgSettingsPage::loadSettings()
{
    m_image->setText(ngSetting("GLASS_IMAGE"));
    m_glass->setValue(qBound(0, ngSettingInt("GLASS", 55), 100));
    m_blur->setValue(qBound(0, ngSettingInt("GLASS_BLUR", 45), 100));
    static const NgRowStyle order[] = {NgRowStyle::Card, NgRowStyle::Open, NgRowStyle::Line, NgRowStyle::Stripe, NgRowStyle::None};
    for (int i = 0; i < 5; ++i) {
        if (order[i] == ngRowStyle()) {
            m_rows->setCurrentIndex(i);
        }
    }
    m_twoLines->setChecked(ngSetting("LIST").toLower() != QLatin1String("columns"));
    m_glassPanel->setChecked(ngSetting("SIDEBAR").toLower() != QLatin1String("plain"));
    m_backdrop->setCurrentIndex(qMax(0, m_backdrop->findData(ngSetting("GLASS_MODE").toLower())));
    m_lavaSpeed->setValue(qBound(5, ngSettingInt("LAVA_SPEED", 30), 100));
    m_lavaSize->setValue(qBound(10, ngSettingInt("LAVA_SIZE", 55), 100));
    m_lavaStyle->setCurrentIndex(ngSetting("LAVA_STYLE").toLower() == QLatin1String("blobs") ? 1 : 0);
    static const char *const defaults[3] = {"#2f6f8f", "#8a5a2c", "#3c4a78"};
    const QString spec = ngSetting("LAVA_COLORS").trimmed().toLower();
    const QStringList custom = spec.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const bool isCustom = !spec.isEmpty() && spec != QLatin1String("wallpaper");
    for (int i = 0; i < 3; ++i) {
        const QColor colour(isCustom && i < custom.size() ? custom.at(i).trimmed() : QString::fromLatin1(defaults[i]));
        m_lavaColour[i]->setColor(colour.isValid() ? colour : QColor(QString::fromLatin1(defaults[i])));
        m_lavaColour[i]->setEnabled(isCustom);
    }
    m_lavaColours->setCurrentIndex(spec.isEmpty() ? 0 : spec == QLatin1String("wallpaper") ? 1 : 2);
    m_devices->setChecked(ngSetting("DEVICES").toLower() == QLatin1String("show"));
    m_sigla->setCurrentIndex(qMax(0, m_sigla->findData(ngSetting("SIGLA", QStringLiteral("fill")).toLower())));
    m_mark->setCurrentIndex(qMax(0, m_mark->findData(ngSetting("MARK").toLower())));
    m_icons->setChecked(ngSetting("ICONS").toLower() == QLatin1String("show"));
    m_tiles->setChecked(ngSetting("TILES").toLower() != QLatin1String("off"));
}

void NgSettingsPage::applySettings()
{
    setNeoSetting("GLASS_IMAGE", m_image->text().trimmed());
    setNeoSetting("GLASS", QString::number(m_glass->value()));
    setNeoSetting("GLASS_BLUR", QString::number(m_blur->value()));
    setNeoSetting("SIGLA", m_sigla->currentData().toString());
    setNeoSetting("MARK", m_mark->currentData().toString());
    setNeoSetting("ICONS", m_icons->isChecked() ? QStringLiteral("show") : QString());
    setNeoSetting("TILES", m_tiles->isChecked() ? QString() : QStringLiteral("off"));
    setNeoSetting("LIST", m_twoLines->isChecked() ? QString() : QStringLiteral("columns"));
    setNeoSetting("SIDEBAR", m_glassPanel->isChecked() ? QString() : QStringLiteral("plain"));
    setNeoSetting("GLASS_MODE", m_backdrop->currentData().toString());
    setNeoSetting("LAVA_SPEED", QString::number(m_lavaSpeed->value()));
    setNeoSetting("LAVA_SIZE", QString::number(m_lavaSize->value()));
    setNeoSetting("LAVA_STYLE", m_lavaStyle->currentData().toString());
    const int colours = m_lavaColours->currentIndex();
    setNeoSetting("LAVA_COLORS",
                  colours == 0   ? QString()
                  : colours == 1 ? QStringLiteral("wallpaper")
                                 : QStringLiteral("%1,%2,%3").arg(m_lavaColour[0]->color().name(), m_lavaColour[1]->color().name(), m_lavaColour[2]->color().name()));
    setNeoSetting("DEVICES", m_devices->isChecked() ? QStringLiteral("show") : QString());
    if (m_rows->isEnabled()) {
        static const NgRowStyle order[] = {NgRowStyle::Card, NgRowStyle::Open, NgRowStyle::Line, NgRowStyle::Stripe, NgRowStyle::None};
        setNeoRowStyle(order[qBound(0, m_rows->currentIndex(), 4)]);
    }

    // sidebar and row style apply at once: repaint everything
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
        widget->update();
        if (auto *graphicsView = qobject_cast<QGraphicsView *>(widget)) {
            graphicsView->viewport()->update();
        }
    }
}

void NgSettingsPage::restoreDefaults()
{
    m_image->clear();
    m_glass->setValue(55);
    m_blur->setValue(45);
    m_rows->setCurrentIndex(0);
    m_sigla->setCurrentIndex(0);
    m_mark->setCurrentIndex(0);
    m_icons->setChecked(false);
    m_tiles->setChecked(true);
    m_twoLines->setChecked(true);
    m_glassPanel->setChecked(true);
    m_backdrop->setCurrentIndex(0);
    m_lavaSpeed->setValue(30);
    m_lavaSize->setValue(55);
    m_lavaStyle->setCurrentIndex(0);
    m_lavaColours->setCurrentIndex(0);
    m_devices->setChecked(false);
}

#include "moc_ngsettingspage.cpp"

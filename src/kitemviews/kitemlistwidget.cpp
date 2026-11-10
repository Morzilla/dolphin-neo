/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * Based on the Itemviews NG project from Trolltech Labs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemlistwidget.h"

#include "kitemlistview.h"
#include "private/kitemlistselectiontoggle.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDateTime>
#include <QHash>

#include <QApplication>
#include <QSet>
#include <QLinearGradient>
#include <QFileInfo>
#include <QStandardPaths>
#include <tuple>
#include <QPainter>
#include <QPropertyAnimation>
#include <QStyleOption>

KItemListWidgetInformant::KItemListWidgetInformant() = default;

KItemListWidgetInformant::~KItemListWidgetInformant() = default;

KItemListWidget::KItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
    : QGraphicsWidget(parent)
    , m_informant(informant)
    , m_index(-1)
    , m_selected(false)
    , m_current(false)
    , m_hovered(false)
    , m_expansionAreaHovered(false)
    , m_alternateBackground(false)
    , m_enabledSelectionToggle(false)
    , m_clickHighlighted(false)
    , m_data()
    , m_visibleRoles()
    , m_columnWidths()
    , m_leftPadding(0)
    , m_rightPadding(0)
    , m_styleOption()
    , m_siblingsInfo()
    , m_hoverOpacity(0)
    , m_hoverCache(nullptr)
    , m_hoverSequenceIndex(0)
    , m_selectionToggle(nullptr)
    , m_editedRole()
    , m_iconSize(-1)
{
    connect(&m_hoverSequenceTimer, &QTimer::timeout, this, &KItemListWidget::slotHoverSequenceTimerTimeout);
}

KItemListWidget::~KItemListWidget()
{
    clearHoverCache();
}

void KItemListWidget::setIndex(int index)
{
    if (m_index != index) {
        delete m_selectionToggle;
        m_selectionToggle = nullptr;

        m_hoverOpacity = 0;

        clearHoverCache();

        m_index = index;
    }
}

int KItemListWidget::index() const
{
    return m_index;
}

void KItemListWidget::setData(const QHash<QByteArray, QVariant> &data, const QSet<QByteArray> &roles)
{
    clearHoverCache();
    if (roles.isEmpty()) {
        m_data = data;
        dataChanged(m_data);
    } else {
        for (const QByteArray &role : roles) {
            m_data[role] = data[role];
        }
        dataChanged(m_data, roles);
    }
    update();
}

QHash<QByteArray, QVariant> KItemListWidget::data() const
{
    return m_data;
}

QVariant KItemListWidget::value(const QByteArray &key) const
{
    return m_data.value(key);
}

namespace
{
struct NgCached {
    bool present = false;
    QString value;
};
QHash<QByteArray, NgCached> &ngCache()
{
    static QHash<QByteArray, NgCached> cache;
    return cache;
}
int ngGeneration = 0;
}

NgSettings *NgSettings::instance()
{
    static NgSettings settings;
    return &settings;
}

void ngSettingsInvalidate()
{
    ngCache().clear();
    ++ngGeneration;
    Q_EMIT NgSettings::instance()->changed();
}

int ngSettingsGeneration()
{
    return ngGeneration;
}

// Values are cached: the row painting asks for them many times per frame. ngSettingsInvalidate() clears the cache.
QString ngSetting(const char *name, const QString &fallback)
{
    const QByteArray key(name);
    auto it = ngCache().constFind(key);
    if (it == ngCache().constEnd()) {
        NgCached entry;
        const QByteArray variable = QByteArray("DOLPHIN_NG_") + name;
        if (qEnvironmentVariableIsSet(variable.constData())) {
            entry.present = true;
            entry.value = qEnvironmentVariable(variable.constData());
        } else {
            const KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
            entry.present = group.hasKey(name);
            entry.value = group.readEntry(name, QString());
        }
        it = ngCache().insert(key, entry);
    }
    return it->present ? it->value : fallback;
}

int ngSettingInt(const char *name, int fallback)
{
    bool ok = false;
    const int value = ngSetting(name).toInt(&ok);
    return ok ? value : fallback;
}

void setNgSetting(const char *name, const QString &value)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
    if (value.isEmpty()) {
        group.deleteEntry(name);
    } else {
        group.writeEntry(name, value);
    }
    group.sync();
    ngCache().remove(QByteArray(name));
}

bool ngSettingFromEnvironment(const char *name)
{
    const QByteArray variable = QByteArray("DOLPHIN_NG_") + name;
    return qEnvironmentVariableIsSet(variable.constData());
}

NgCardStyle ngCardStyle()
{
    const QString look = ngSetting("CARDS").toLower();
    if (look == QLatin1String("b") || look == QLatin1String("scheda")) {
        return NgCardStyle::Scheda;
    }
    if (look == QLatin1String("c") || look == QLatin1String("dossier")) {
        return NgCardStyle::Dossier;
    }
    return NgCardStyle::Tessera;
}

bool ngCardsStretch()
{
    return ngSetting("CARDS_FIT").toLower() == QLatin1String("stretch");
}

QList<NgQuickAction> ngQuickActionsFor(const QString &localPath, const QString &mime)
{
    QList<NgQuickAction> actions;
    if (localPath.isEmpty() || mime.isEmpty() || mime == QLatin1String("inode/directory")) {
        return actions;
    }
    const QFileInfo info(localPath);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    const QString lowerSuffix = suffix.toLower();
    const bool isImage = mime.startsWith(QLatin1String("image/")) && !mime.contains(QLatin1String("svg"));
    static const QSet<QString> archives{QStringLiteral("application/zip"),
                                        QStringLiteral("application/x-tar"),
                                        QStringLiteral("application/x-compressed-tar"),
                                        QStringLiteral("application/x-bzip-compressed-tar"),
                                        QStringLiteral("application/x-xz-compressed-tar"),
                                        QStringLiteral("application/x-zstd-compressed-tar"),
                                        QStringLiteral("application/x-7z-compressed"),
                                        QStringLiteral("application/vnd.rar"),
                                        QStringLiteral("application/x-rar"),
                                        QStringLiteral("application/gzip"),
                                        QStringLiteral("application/x-bzip"),
                                        QStringLiteral("application/x-xz"),
                                        QStringLiteral("application/zstd")};
    auto simple = [&actions](const QString &label, const QString &program, const QStringList &arguments) {
        NgQuickAction action;
        action.label = label;
        action.program = program;
        action.arguments = arguments;
        actions.append(action);
    };
    if (isImage) {
        NgQuickAction resize;
        resize.label = QStringLiteral("Ridimensiona…");
        resize.program = QStringLiteral("magick");
        for (const auto &[label, spec, tag] : {std::make_tuple(QStringLiteral("75 %"), QStringLiteral("75%"), QStringLiteral("75")),
                                               std::make_tuple(QStringLiteral("50 %"), QStringLiteral("50%"), QStringLiteral("50")),
                                               std::make_tuple(QStringLiteral("25 %"), QStringLiteral("25%"), QStringLiteral("25")),
                                               std::make_tuple(QStringLiteral("Larghezza 1920"), QStringLiteral("1920x>"), QStringLiteral("1920")),
                                               std::make_tuple(QStringLiteral("Larghezza 1280"), QStringLiteral("1280x>"), QStringLiteral("1280")),
                                               std::make_tuple(QStringLiteral("Larghezza 800"), QStringLiteral("800x>"), QStringLiteral("800"))}) {
            resize.choices.append({label, {localPath, QStringLiteral("-resize"), spec, QStringLiteral("%1-%2.%3").arg(base, tag, suffix)}});
        }
        actions.append(resize);
        // "open with": the runner (DolphinView) fills the menu with the applications for this type
        simple(QStringLiteral("Apri con…"), QStringLiteral("@openwith"), {});
        if (mime == QLatin1String("image/gif")) {
            simple(QStringLiteral("In MP4"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-movflags"), QStringLiteral("faststart"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-vf"), QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2"), QStringLiteral("%1.mp4").arg(base)});
        }
    } else if (archives.contains(mime)) {
        static const bool hasArk = !QStandardPaths::findExecutable(QStringLiteral("ark")).isEmpty();
        if (hasArk) {
            simple(QStringLiteral("Estrai"), QStringLiteral("ark"), {QStringLiteral("--batch"), QStringLiteral("--autosubfolder"), localPath});
        } else {
            simple(QStringLiteral("Estrai"), QStringLiteral("bsdtar"), {QStringLiteral("-xf"), localPath, QStringLiteral("--one-top-level=%1").arg(base)});
        }
    } else if (mime == QLatin1String("application/pdf")) {
        simple(QStringLiteral("Testo"), QStringLiteral("pdftotext"), {QStringLiteral("-layout"), localPath, QStringLiteral("%1.txt").arg(base)});
        simple(QStringLiteral("Pagine PNG"), QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("150"), localPath, base});
    } else if (mime.startsWith(QLatin1String("audio/"))) {
        if (lowerSuffix != QLatin1String("mp3")) {
            simple(QStringLiteral("In MP3"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
        }
        if (lowerSuffix != QLatin1String("flac")) {
            simple(QStringLiteral("In FLAC"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("%1.flac").arg(base)});
        }
    } else if (mime.startsWith(QLatin1String("video/"))) {
        simple(QStringLiteral("In MP4"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("%1.mp4").arg(base)});
        simple(QStringLiteral("Audio MP3"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-vn"), QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
        simple(QStringLiteral("Fotogramma"), QStringLiteral("ffmpeg"), {QStringLiteral("-ss"), QStringLiteral("1"), QStringLiteral("-i"), localPath, QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("%1-1s.png").arg(base)});
    }
    return actions;
}

namespace
{
NgRowStyle ngRowStyleFromName(const QByteArray &name, NgRowStyle fallback)
{
    return name == "stripe" ? NgRowStyle::Stripe
        : name == "line"    ? NgRowStyle::Line
        : name == "none"    ? NgRowStyle::None
        : name == "open"    ? NgRowStyle::Open
        : name == "card"    ? NgRowStyle::Card
                            : fallback;
}
// the current style: DOLPHIN_NG_ROWS if set, else what was last chosen in the window, else cards
NgRowStyle &ngRowStyleRef()
{
    static NgRowStyle style = NgRowStyle::Card;
    static int generation = -1;
    if (generation != ngSettingsGeneration()) {
        generation = ngSettingsGeneration();
        const QByteArray saved = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG")).readEntry("RowStyle", QByteArray("card")).toLower();
        style = ngRowStyleFromName(qgetenv("DOLPHIN_NG_ROWS").toLower(), ngRowStyleFromName(saved, NgRowStyle::Card));
    }
    return style;
}
}

NgRowStyle ngRowStyle()
{
    return ngRowStyleRef();
}

void setNgRowStyle(NgRowStyle style)
{
    ngRowStyleRef() = style;
    static const char *const names[] = {"stripe", "card", "line", "none", "open"};
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
    group.writeEntry("RowStyle", QByteArray(names[int(style)]));
    group.sync();
}

bool ngTwoLineList()
{
    const bool columns = ngSetting("LIST").toLower() == QLatin1String("columns");
    return !columns && QGuiApplication::layoutDirection() == Qt::LeftToRight;
}

QColor ngCategoryTint(NgKind::Category category)
{
    static const QColor tints[NgKind::CategoryCount] = {QColor(201, 205, 213), QColor(195, 164, 230), QColor(143, 188, 230), QColor(127, 214, 168), QColor(230, 183, 122),
                                                        QColor(127, 208, 193), QColor(230, 164, 200), QColor(240, 163, 154), QColor(176, 181, 191)};
    return tints[category];
}

NgKind ngKindFor(const QString &name, bool isDir)
{
    auto kind = [](const QString &sigla, NgKind::Category category) {
        return NgKind{sigla, ngCategoryTint(category), category};
    };
    if (isDir) {
        return kind(QStringLiteral("DIR"), NgKind::Folder);
    }
    const QString lower = name.toLower();
    static const QList<std::pair<QString, QString>> compound{{QStringLiteral(".tar.gz"), QStringLiteral("TGZ")},
                                                             {QStringLiteral(".tar.xz"), QStringLiteral("TXZ")},
                                                             {QStringLiteral(".tar.bz2"), QStringLiteral("TBZ")},
                                                             {QStringLiteral(".tar.zst"), QStringLiteral("TZST")}};
    for (const auto &entry : compound) {
        if (lower.endsWith(entry.first)) {
            return kind(entry.second, NgKind::Archive);
        }
    }
    const int dot = lower.lastIndexOf(QLatin1Char('.'));
    const QString ext = (dot > 0 && dot < lower.size() - 1) ? lower.mid(dot + 1) : QString();
    if (ext.isEmpty() || ext.size() > 5) {
        return kind(QStringLiteral("FILE"), NgKind::Other);
    }
    static const QSet<QString> images{"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "tif", "tiff", "heic", "avif", "ico", "xcf", "kra"};
    static const QSet<QString> documents{"pdf", "doc", "docx", "odt", "rtf", "txt", "md", "epub", "ppt", "pptx", "odp", "msg", "eml"};
    static const QSet<QString> sheets{"xls", "xlsx", "ods", "csv", "tsv"};
    static const QSet<QString> archives{"zip", "gz", "tgz", "xz", "bz2", "7z", "rar", "tar", "zst"};
    static const QSet<QString> medias{"mp3", "flac", "ogg", "wav", "m4a", "opus", "mp4", "mkv", "webm", "avi", "mov"};
    static const QSet<QString> datas{"json", "xml", "yaml", "yml", "toml", "ini", "conf", "html", "css", "js", "ts", "py", "rs", "c", "cpp", "h", "sh", "sql", "log"};
    static const QSet<QString> packages{"rpm", "deb", "exe", "msi", "iso", "img", "apk", "jar"};
    const NgKind::Category category = images.contains(ext) ? NgKind::Image
        : documents.contains(ext)                           ? NgKind::Document
        : sheets.contains(ext)                              ? NgKind::Sheet
        : archives.contains(ext)                            ? NgKind::Archive
        : medias.contains(ext)                              ? NgKind::Media
        : datas.contains(ext)                               ? NgKind::Data
        : packages.contains(ext)                            ? NgKind::Package
                                                            : NgKind::Other;
    return kind(ext.toUpper(), category);
}

// dolphin-ng: "oggi", "ieri", "7 giorni fa", "2 anni fa"
QString ngAgoText(const QDateTime &when)
{
    const qint64 days = when.date().daysTo(QDate::currentDate());
    if (days <= 0) {
        return QStringLiteral("oggi");
    }
    if (days == 1) {
        return QStringLiteral("ieri");
    }
    if (days < 365) {
        return QStringLiteral("%1 giorni fa").arg(days);
    }
    const qint64 years = days / 365;
    return years == 1 ? QStringLiteral("1 anno fa") : QStringLiteral("%1 anni fa").arg(years);
}

void KItemListWidget::ngSettingsChanged()
{
    update();
}

int KItemListWidget::ngActionAt(const QPointF &pos) const
{
    Q_UNUSED(pos)
    return 0;
}

bool ngTiles()
{
    const bool off = ngSetting("TILES").toLower() == QLatin1String("off");
    return !off;
}

bool ngIsDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightnessF() < 0.5;
}

QColor ngAccent(const QColor &colour, bool dark)
{
    float h, s, l;
    colour.getHslF(&h, &s, &l);
    return QColor::fromHslF(h < 0 ? 0 : h, dark ? s : qMin(1.0f, s * 1.15f + 0.05f), dark ? qMax(l, 0.64f) : qMin(l, 0.40f));
}

bool ngNoIcons()
{
    const bool show = ngSetting("ICONS").toLower() == QLatin1String("show");
    return !show && ngTwoLineList();
}

void KItemListWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)

    // dolphin-ng: with any style but "stripe" the view sets m_alternateBackground on every row
    // (see KItemListView::updateAlternateBackgroundForWidget) and the row is drawn here.
    if (m_alternateBackground && ngRowStyle() == NgRowStyle::Card) {
        // a rounded card per row, darker than the view, with a hairline of light along its top edge;
        // same footprint as the hover/selection shape of the style
        const QRectF card = selectionRectFull().adjusted(3, 3, -3, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        const bool dark = ngIsDark(m_styleOption.palette);
        painter->setBrush(dark ? QColor(0, 0, 0, 66) : QColor(0, 0, 0, 14)); // darker than the view on either scheme
        painter->drawRoundedRect(card, 5, 5);
        painter->setPen(QPen(dark ? QColor(255, 255, 255, 11) : QColor(255, 255, 255, 170), 1));
        painter->drawLine(QPointF(card.left() + 5, card.top() + 0.5), QPointF(card.right() - 5, card.top() + 0.5));
        painter->restore();
    } else if (m_alternateBackground && ngRowStyle() == NgRowStyle::Open) {
        // open rows: no card and no separator; the row is told apart by its colour wash alone
    } else if (m_alternateBackground && ngRowStyle() == NgRowStyle::Line) {
        // a hairline under the row that fades out towards both ends
        QColor lineColor = m_styleOption.palette.color(QPalette::Text);
        QColor transparent = lineColor;
        lineColor.setAlphaF(0.10);
        transparent.setAlphaF(0.0);
        QLinearGradient gradient(0, 0, size().width(), 0);
        gradient.setColorAt(0.0, transparent);
        gradient.setColorAt(0.12, lineColor);
        gradient.setColorAt(0.88, lineColor);
        gradient.setColorAt(1.0, transparent);
        painter->fillRect(QRectF(0, size().height() - 1, size().width(), 1), gradient);
    } else if (m_alternateBackground && ngRowStyle() == NgRowStyle::Stripe) {
        QColor backgroundColor = m_styleOption.palette.color(QPalette::AlternateBase);
        if (!widget->hasFocus()) {
            QColor baseColor = m_styleOption.palette.color(QPalette::Base);
            if (baseColor.lightnessF() > 0.5) {
                // theme seems light
                backgroundColor = backgroundColor.lighter(101);
            } else {
                // theme seems dark
                backgroundColor = backgroundColor.darker(101);
            }
        }

        const QRectF backgroundRect(0, 0, size().width(), size().height());
        painter->fillRect(backgroundRect, backgroundColor);
    }

    const QStyle::State selectedState(m_selected ? QStyle::State_Selected : QStyle::State(0));
    if ((m_selected || m_current) && m_editedRole.isEmpty()) {
        const QStyle::State activeState(isActiveWindow() && widget->hasFocus() ? QStyle::State_Active : 0);
        // Only pass State_Selected when the item is actually selected, not just when
        // it has keyboard focus (m_current), to avoid styles drawing a persistent
        // selection highlight on focused but unselected items.
        drawItemStyleOption(painter, widget, activeState | selectedState | QStyle::State_Enabled | QStyle::State_Item);
    }

    if (m_hoverOpacity > 0.0) {
        if (!m_hoverCache) {
            // Initialize the m_hoverCache pixmap to improve the drawing performance
            // when fading the hover background
            m_hoverCache = new QPixmap(size().toSize());
            m_hoverCache->fill(Qt::transparent);

            QPainter pixmapPainter(m_hoverCache);
            const QStyle::State activeState(isActiveWindow() && widget->hasFocus() ? QStyle::State_Active | QStyle::State_Enabled : 0);
            drawItemStyleOption(&pixmapPainter, widget, activeState | selectedState | QStyle::State_MouseOver | QStyle::State_Item);
        }

        const qreal opacity = painter->opacity();
        painter->setOpacity(m_hoverOpacity * opacity);
        painter->drawPixmap(0, 0, *m_hoverCache);
        painter->setOpacity(opacity);
    }
}

void KItemListWidget::setVisibleRoles(const QList<QByteArray> &roles)
{
    const QList<QByteArray> previousRoles = m_visibleRoles;
    m_visibleRoles = roles;

    visibleRolesChanged(roles, previousRoles);
    update();
}

QList<QByteArray> KItemListWidget::visibleRoles() const
{
    return m_visibleRoles;
}

void KItemListWidget::setColumnWidth(const QByteArray &role, qreal width)
{
    const qreal previousWidth = m_columnWidths.value(role);
    if (previousWidth != width) {
        m_columnWidths.insert(role, width);
        columnWidthChanged(role, width, previousWidth);
        update();
    }
}

qreal KItemListWidget::columnWidth(const QByteArray &role) const
{
    return m_columnWidths.value(role);
}

void KItemListWidget::setSidePadding(qreal leftPaddingWidth, qreal rightPaddingWidth)
{
    bool changed = false;
    if (m_leftPadding != leftPaddingWidth) {
        m_leftPadding = leftPaddingWidth;
        changed = true;
    }

    if (m_rightPadding != rightPaddingWidth) {
        m_rightPadding = rightPaddingWidth;
        changed = true;
    }

    if (!changed) {
        return;
    }

    sidePaddingChanged(leftPaddingWidth, rightPaddingWidth);
    update();
}

qreal KItemListWidget::leftPadding() const
{
    return m_leftPadding;
}

qreal KItemListWidget::rightPadding() const
{
    return m_rightPadding;
}

void KItemListWidget::setStyleOption(const KItemListStyleOption &option)
{
    if (m_styleOption == option) {
        return;
    }

    const KItemListStyleOption previous = m_styleOption;
    clearHoverCache();
    m_styleOption = option;
    styleOptionChanged(option, previous);
    update();
}

const KItemListStyleOption &KItemListWidget::styleOption() const
{
    return m_styleOption;
}

void KItemListWidget::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        if (m_selectionToggle) {
            m_selectionToggle->setChecked(selected);
        }
        selectedChanged(selected);
        clearHoverCache();
        update();
    }
}

bool KItemListWidget::isSelected() const
{
    return m_selected;
}

void KItemListWidget::setCurrent(bool current)
{
    if (m_current != current) {
        m_current = current;
        currentChanged(current);
        update();
    }
}

bool KItemListWidget::isCurrent() const
{
    return m_current;
}

void KItemListWidget::setHovered(bool hovered)
{
    if (hovered == m_hovered) {
        return;
    }

    m_hovered = hovered;

    m_hoverSequenceIndex = 0;

    if (hovered) {
        setHoverOpacity(1.0);

        if (m_enabledSelectionToggle && !(QApplication::mouseButtons() & Qt::LeftButton)) {
            initializeSelectionToggle();
        }

        hoverSequenceStarted();

        const KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
        const int interval = globalConfig.readEntry("HoverSequenceInterval", 700);

        m_hoverSequenceTimer.start(interval);
    } else {
        setHoverOpacity(0.0);

        if (m_selectionToggle) {
            m_selectionToggle->deleteLater();
            m_selectionToggle = nullptr;
        }

        hoverSequenceEnded();
        m_hoverSequenceTimer.stop();
    }

    hoveredChanged(hovered);
    update();
}

bool KItemListWidget::isHovered() const
{
    return m_hovered;
}

void KItemListWidget::setExpansionAreaHovered(bool hovered)
{
    if (hovered == m_expansionAreaHovered) {
        return;
    }
    m_expansionAreaHovered = hovered;
    update();
}

bool KItemListWidget::expansionAreaHovered() const
{
    return m_expansionAreaHovered;
}

void KItemListWidget::setHoverPosition(const QPointF &pos)
{
    if (m_selectionToggle) {
        m_selectionToggle->setHovered(selectionToggleRect().contains(pos));
    }
}

void KItemListWidget::setAlternateBackground(bool enable)
{
    if (m_alternateBackground != enable) {
        m_alternateBackground = enable;
        alternateBackgroundChanged(enable);
        update();
    }
}

bool KItemListWidget::alternateBackground() const
{
    return m_alternateBackground;
}

void KItemListWidget::setEnabledSelectionToggle(bool enable)
{
    if (m_enabledSelectionToggle != enable) {
        m_enabledSelectionToggle = enable;

        // We want the change to take effect immediately.
        if (m_enabledSelectionToggle) {
            if (m_hovered) {
                initializeSelectionToggle();
            }
        } else if (m_selectionToggle) {
            m_selectionToggle->deleteLater();
            m_selectionToggle = nullptr;
        }

        update();
    }
}

bool KItemListWidget::enabledSelectionToggle() const
{
    return m_enabledSelectionToggle;
}

void KItemListWidget::setSiblingsInformation(const QBitArray &siblings)
{
    const QBitArray previous = m_siblingsInfo;
    m_siblingsInfo = siblings;
    siblingsInformationChanged(m_siblingsInfo, previous);
    update();
}

QBitArray KItemListWidget::siblingsInformation() const
{
    return m_siblingsInfo;
}

void KItemListWidget::setEditedRole(const QByteArray &role)
{
    if (m_editedRole != role) {
        const QByteArray previous = m_editedRole;
        m_editedRole = role;
        editedRoleChanged(role, previous);
    }
}

QByteArray KItemListWidget::editedRole() const
{
    return m_editedRole;
}

void KItemListWidget::setIconSize(int iconSize)
{
    if (m_iconSize != iconSize) {
        const int previousIconSize = m_iconSize;
        m_iconSize = iconSize;
        iconSizeChanged(iconSize, previousIconSize);
    }
}

int KItemListWidget::iconSize() const
{
    return m_iconSize;
}

bool KItemListWidget::contains(const QPointF &point) const
{
    if (!QGraphicsWidget::contains(point)) {
        return false;
    }

    return selectionRectFull().contains(point) || expansionToggleRect().contains(point);
}

QRectF KItemListWidget::textFocusRect() const
{
    return textRect();
}

QRectF KItemListWidget::selectionToggleRect() const
{
    return QRectF();
}

QRectF KItemListWidget::expansionToggleRect() const
{
    return QRectF();
}

QPixmap KItemListWidget::createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QPixmap pixmap(size().toSize() * widget->devicePixelRatio());
    pixmap.setDevicePixelRatio(widget->devicePixelRatio());
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);

    const bool oldAlternateBackground = m_alternateBackground;
    const bool wasSelected = m_selected;
    const bool wasHovered = m_hovered;

    setAlternateBackground(false);
    setHovered(false);

    paint(&painter, option, widget);

    setAlternateBackground(oldAlternateBackground);
    setSelected(wasSelected);
    setHovered(wasHovered);

    return pixmap;
}

void KItemListWidget::startActivateSoonAnimation(int timeUntilActivation)
{
    Q_UNUSED(timeUntilActivation)
}

void KItemListWidget::dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles)
{
    Q_UNUSED(current)
    Q_UNUSED(roles)
}

void KItemListWidget::visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::columnWidthChanged(const QByteArray &role, qreal current, qreal previous)
{
    Q_UNUSED(role)
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth)
{
    Q_UNUSED(leftPaddingWidth)
    Q_UNUSED(rightPaddingWidth)
}

void KItemListWidget::styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous)
{
    Q_UNUSED(previous)

    // set the initial value of m_iconSize if not set
    if (m_iconSize == -1) {
        m_iconSize = current.iconSize;
    }
}

void KItemListWidget::currentChanged(bool current)
{
    Q_UNUSED(current)
}

void KItemListWidget::selectedChanged(bool selected)
{
    Q_UNUSED(selected)
}

void KItemListWidget::hoveredChanged(bool hovered)
{
    Q_UNUSED(hovered)
}

void KItemListWidget::pressedChanged(bool pressed)
{
    Q_UNUSED(pressed)
}

void KItemListWidget::alternateBackgroundChanged(bool enabled)
{
    Q_UNUSED(enabled)
}

void KItemListWidget::siblingsInformationChanged(const QBitArray &current, const QBitArray &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::editedRoleChanged(const QByteArray &current, const QByteArray &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::iconSizeChanged(int current, int previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::resizeEvent(QGraphicsSceneResizeEvent *event)
{
    QGraphicsWidget::resizeEvent(event);
    clearHoverCache();

    if (m_selectionToggle) {
        const QRectF &toggleRect = selectionToggleRect();
        m_selectionToggle->setPos(toggleRect.topLeft());
        m_selectionToggle->resize(toggleRect.size());
    }
}

void KItemListWidget::hoverSequenceStarted()
{
}

void KItemListWidget::hoverSequenceIndexChanged(int sequenceIndex)
{
    Q_UNUSED(sequenceIndex);
}

void KItemListWidget::hoverSequenceEnded()
{
}

qreal KItemListWidget::hoverOpacity() const
{
    return m_hoverOpacity;
}

int KItemListWidget::hoverSequenceIndex() const
{
    return m_hoverSequenceIndex;
}

void KItemListWidget::slotHoverSequenceTimerTimeout()
{
    m_hoverSequenceIndex++;
    hoverSequenceIndexChanged(m_hoverSequenceIndex);
}

void KItemListWidget::initializeSelectionToggle()
{
    Q_ASSERT(m_enabledSelectionToggle);

    if (!m_selectionToggle) {
        m_selectionToggle = new KItemListSelectionToggle(this);
    }

    const QRectF toggleRect = selectionToggleRect();
    m_selectionToggle->setPos(toggleRect.topLeft());
    m_selectionToggle->resize(toggleRect.size());

    m_selectionToggle->setChecked(isSelected());
}

void KItemListWidget::setHoverOpacity(qreal opacity)
{
    m_hoverOpacity = opacity;
    if (m_selectionToggle) {
        m_selectionToggle->setOpacity(opacity);
    }

    if (m_hoverOpacity <= 0.0) {
        delete m_hoverCache;
        m_hoverCache = nullptr;
    }

    update();
}

void KItemListWidget::clearHoverCache()
{
    delete m_hoverCache;
    m_hoverCache = nullptr;
}

bool KItemListWidget::isPressed() const
{
    return m_clickHighlighted;
}

void KItemListWidget::setPressed(bool enabled)
{
    if (m_clickHighlighted != enabled) {
        m_clickHighlighted = enabled;
        clearHoverCache();
        pressedChanged(enabled);
        update();
    }
}

void KItemListWidget::drawItemStyleOption(QPainter *painter, QWidget *widget, QStyle::State styleState)
{
    if (m_alternateBackground && ngTwoLineList()) {
        // dolphin-ng: the rows draw their own hover and selection, so the look does not depend on the widget
        // style (Breeze would fill the whole row with the highlight colour): a neutral veil, stronger on the left
        const bool selected = styleState & QStyle::State_Selected;
        const bool hovered = styleState & QStyle::State_MouseOver;
        if (!selected && !hovered) {
            return;
        }
        const bool dark = ngIsDark(widget ? widget->palette() : m_styleOption.palette);
        const QRectF card = selectionRectFull().adjusted(3, 3, -3, -3);
        const qreal strength = selected ? (hovered ? 1.0 : 0.85) : 0.35;
        QLinearGradient veil(card.topLeft(), card.topRight());
        const int base = dark ? 255 : 0;
        veil.setColorAt(0.0, QColor(base, base, base, qRound((dark ? 72 : 40) * strength)));
        veil.setColorAt(1.0, QColor(base, base, base, qRound((dark ? 18 : 10) * strength)));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(veil);
        painter->drawRoundedRect(card, 5, 5);
        if (selected) {
            // a thin edge in the highlight colour marks the selection without filling it
            QColor edge = m_styleOption.palette.color(QPalette::Highlight);
            edge.setAlphaF(0.55);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(edge, 1));
            painter->drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        }
        painter->restore();
        return;
    }

    painter->save();
    QStyleOptionViewItem viewItemOption;
    initStyleOption(&viewItemOption);
    viewItemOption.state = styleState;
    viewItemOption.viewItemPosition = QStyleOptionViewItem::OnlyOne;
    viewItemOption.showDecorationSelected = true;
    viewItemOption.rect = selectionRectFull().toRect();
    const bool current = m_current && styleState & QStyle::State_Active;

    // TODO: Remove this check after Plasma 6.8 release
    // See: https://invent.kde.org/plasma/breeze/-/merge_requests/595
    if (style()->name() == QStringLiteral("breeze")) {
        QColor backgroundColor{widget->palette().color(QPalette::Highlight)};
        backgroundColor.setAlphaF(0.0);
        if (m_clickHighlighted) {
            backgroundColor.setAlphaF(1.0);
        } else {
            if (m_selected && m_hovered) {
                backgroundColor.setAlphaF(0.40);
            } else if (m_selected) {
                backgroundColor.setAlphaF(0.32);
            } else if (m_hovered) {
                backgroundColor = widget->palette().color(QPalette::Text);
                backgroundColor.setAlphaF(0.06);
            }
        }
        painter->setRenderHint(QPainter::Antialiasing);
        constexpr int roundness = 5; // From Breeze style.
        constexpr qreal penWidth = 1.25;
        QPainterPath path;
        const qreal adjustment = 0.5 * penWidth; // Use same adjustments as Breeze strokedRect uses, to snap to pixelGrid.
        path.addRoundedRect(selectionRectFull().adjusted(adjustment, adjustment, -adjustment, -adjustment), roundness, roundness);
        painter->fillPath(path, backgroundColor);

        // Focus decoration
        if (current) {
            QColor focusColor{widget->palette().color(QPalette::Highlight)};
            // Set the pen color lighter or darker depending on background color
            focusColor = m_styleOption.palette.color(QPalette::Base).lightnessF() > 0.5 ? focusColor.darker(110) : focusColor.lighter(110);
            focusColor.setAlphaF(m_selected || m_hovered ? 1.0 : 0.8);
            QPen pen{focusColor, penWidth};
            pen.setCosmetic(true);
            painter->strokePath(path, pen);
        }
    } else {
        style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &viewItemOption, painter, widget);

        // Focus decoration (dolphin-ng: not in the two-line list, where it shows as a light line under the row)
        if (current && !(m_alternateBackground && ngTwoLineList())) {
            QStyleOptionFocusRect focusRectOption;
            initStyleOption(&focusRectOption);
            focusRectOption.state = QStyle::State_HasFocus;
            if (m_selected && widget->hasFocus()) {
                focusRectOption.state = QStyle::State_HasFocus | QStyle::State_Selected;
            }
            if (m_hovered) {
                 focusRectOption.state |= QStyle::State_MouseOver;
            }
            focusRectOption.rect = viewItemOption.rect;
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focusRectOption, painter, widget);
        }
    }
    painter->restore();
}

#include "moc_kitemlistwidget.cpp"

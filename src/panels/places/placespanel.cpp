/*
 * SPDX-FileCopyrightText: 2008-2012 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2021 Kai Uwe Broulik <kde@broulik.de>
 *
 * Based on KFilePlacesView from kdelibs:
 * SPDX-FileCopyrightText: 2007 Kevin Ottens <ervin@kde.org>
 * SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "placespanel.h"

#include <QGraphicsOpacityEffect>
#include <QLinearGradient>
#include <QScrollBar>

#include "dolphindockwidget.h"

#include "kitemviews/kitemlistwidget.h"

#include "dolphin_generalsettings.h"
#include "dolphin_placespanelsettings.h"
#include "dolphinplacesmodelsingleton.h"
#include "settings/dolphinsettingsdialog.h"
#include "views/draganddrophelper.h"

#include <KFilePlacesModel>
#include <KIO/DropJob>
#include <KIO/Job>
#include <KLocalizedString>
#include <KProtocolManager>

#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionViewItem>
#include <QCursor>
#include <QShowEvent>
#include <QTimer>

#include <Solid/StorageAccess>

PlacesPanel::PlacesPanel(QWidget *parent)
    : KFilePlacesView(parent)
{
    m_ngPalette = palette(); // the stock palette, restored when the glass panel is turned off
    ngApplyPalette();
    setViewportMargins(10, 12, 10, 12 + 62); // dolphin-ng: keep the entries inside the glass card drawn by the dock, above its disk usage box
    // dolphin-ng: a tighter list than KIO's own, and icons moved in so the selection pill's round end clears them
    // (read by Dialog NG's places view; the stock KIO ignores them)
    setProperty("ngRowPadding", 2);
    setProperty("ngIconIndent", 7);
    // dolphin-ng: never fill the list background, whatever the style's base colour is, so the glass card shows through
    viewport()->setAutoFillBackground(false);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NoSystemBackground);
    viewport()->setAttribute(Qt::WA_NoSystemBackground);
    setDropOnPlaceEnabled(true);
    connect(this, &PlacesPanel::urlsDropped, this, &PlacesPanel::slotUrlsDropped);

    setAutoResizeItemsEnabled(false);

    setTeardownFunction([this](const QModelIndex &index) {
        slotTearDownRequested(index);
    });

    m_openInSplitView = std::make_unique<QAction>(QIcon::fromTheme(QStringLiteral("view-split-left-right")), i18nc("@action:inmenu", "Open in Split View"));
    m_openInSplitView->setPriority(QAction::HighPriority);
    connect(m_openInSplitView.get(), &QAction::triggered, this, [this]() {
        const QUrl url = currentIndex().data(KFilePlacesModel::UrlRole).toUrl();
        Q_EMIT openInSplitViewRequested(url);
    });
    addAction(m_openInSplitView.get());

    m_configureTrashAction = std::make_unique<QAction>(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Configure Trash…"));
    m_configureTrashAction->setPriority(QAction::HighPriority);
    connect(m_configureTrashAction.get(), &QAction::triggered, this, &PlacesPanel::slotConfigureTrash);
    addAction(m_configureTrashAction.get());

    connect(this, &PlacesPanel::contextMenuAboutToShow, this, &PlacesPanel::slotContextMenuAboutToShow);

    connect(this, &PlacesPanel::iconSizeChanged, this, [](const QSize &newSize) {
        int iconSize = qMin(newSize.width(), newSize.height());
        if (iconSize == 0) {
            // Don't store 0 size, let's keep -1 for default/small/automatic
            iconSize = -1;
        }
        PlacesPanelSettings *settings = PlacesPanelSettings::self();
        settings->setIconSize(iconSize);
        settings->save();
    });

    readSettings();

    // Set the model here so that it's loaded in time for the sizeHint to properly apply (setting it upon showEvent is too late)
    auto *placesModel = DolphinPlacesModelSingleton::instance().placesModel();
    setModel(placesModel);

    // dolphin-ng: the disk usage box at the bottom of the card replaces the "Devices" section (internal disks);
    // removable devices keep their own section. DOLPHIN_NG_DEVICES=show keeps the section.
    // KFilePlacesView decides row visibility itself on model changes, so hide the rows again right after it
    auto applyDevices = [this, placesModel] {
        QTimer::singleShot(0, this, [this, placesModel] {
            ngApplyDevices(placesModel);
        });
    };
    connect(placesModel, &QAbstractItemModel::rowsInserted, this, applyDevices);
    connect(placesModel, &QAbstractItemModel::rowsRemoved, this, applyDevices);
    connect(placesModel, &QAbstractItemModel::dataChanged, this, applyDevices);
    connect(placesModel, &QAbstractItemModel::modelReset, this, applyDevices);
    connect(placesModel, &KFilePlacesModel::groupHiddenChanged, this, applyDevices);
    connect(placesModel, &KFilePlacesModel::reloaded, this, applyDevices);
    applyDevices();
    connect(NgSettings::instance(), &NgSettings::changed, this, [this, placesModel] {
        ngApplyPalette();
        ngApplyDevices(placesModel);
        viewport()->update();
        update();
    });

    connect(placesModel, &KFilePlacesModel::errorMessage, this, &PlacesPanel::errorMessage);
    connect(placesModel, &KFilePlacesModel::teardownDone, this, &PlacesPanel::slotTearDownDone);

    connect(placesModel, &QAbstractItemModel::rowsInserted, this, &PlacesPanel::slotRowsInserted);
    connect(placesModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PlacesPanel::slotRowsAboutToBeRemoved);

    for (int i = 0; i < model()->rowCount(); ++i) {
        connectDeviceSignals(model()->index(i, 0, QModelIndex()));
    }
}

PlacesPanel::~PlacesPanel() = default;

// dolphin-ng: the selected entry sits on a glass pill like the section titles; the hovered entry gets a fainter one.
// The widget style frames and fills those rows in its own way whatever the palette says, so each such row is
// painted again from scratch: the panel's backdrop, the pill, then icon and text by the delegate without the
// selected/hovered state (it draws only the contents then).
void PlacesPanel::ngPaintPills()
{
    auto *dock = qobject_cast<DolphinDockWidget *>(parentWidget());
    if (!dock) {
        return;
    }
    const QModelIndexList selected = selectionModel() ? selectionModel()->selectedIndexes() : QModelIndexList();
    QList<std::pair<QModelIndex, qreal>> rows;
    for (const QModelIndex &index : selected) {
        rows.append({index, 1.0});
    }
    if (viewport()->underMouse()) {
        const QModelIndex hovered = indexAt(viewport()->mapFromGlobal(QCursor::pos()));
        if (hovered.isValid() && !selected.contains(hovered)) {
            rows.append({hovered, 0.5});
        }
    }
    if (rows.isEmpty()) {
        return;
    }
    const bool dark = ngIsDark(palette());
    const QPoint offset = viewport()->mapTo(dock, QPoint(0, 0));
    // the first row of a group is taller: its rect holds the group title too. The plain row height is the
    // smallest one around; the pill keeps to the bottom part of a tall rect.
    int rowHeight = -1;
    if (const QAbstractItemModel *placesModel = model()) {
        for (int row = 0; row < placesModel->rowCount(); ++row) {
            if (isRowHidden(row)) {
                continue;
            }
            const int h = visualRect(placesModel->index(row, 0)).height();
            if (h > 0 && (rowHeight < 0 || h < rowHeight)) {
                rowHeight = h;
            }
        }
    }
    QPainter painter(viewport());
    for (const auto &[index, strength] : std::as_const(rows)) {
        QRect rect = visualRect(index);
        if (!rect.isValid()) {
            continue;
        }
        if (rowHeight > 0 && rect.height() > rowHeight + 2) {
            rect.setTop(rect.bottom() - rowHeight + 1);
        }
        painter.save();
        painter.setClipRect(rect);
        // 1. the glass backdrop, exactly what lies behind the row
        // (with the live blur the glass is translucent: start again from the window colour, which is what the main
        // window paints under the panel before the glass thins it out. Starting from nothing left these rows fully
        // see-through, a bright band over a light desktop)
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect, window()->palette().color(QPalette::Window));
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.save();
        painter.translate(-offset);
        dock->paintNgGlass(painter);
        painter.restore();
        // 2. the pill, the same as the section titles: a faint white veil with a hairline edge, round ends
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF pill = QRectF(rect).adjusted(2.5, 2.5, -2.5, -2.5);
        const qreal radius = pill.height() / 2.0;
        painter.setPen(QPen(dark ? QColor(255, 255, 255, qRound(46 * strength)) : QColor(0, 0, 0, qRound(30 * strength)), 1));
        painter.setBrush(dark ? QColor(255, 255, 255, qRound(30 * strength)) : QColor(255, 255, 255, qRound(150 * strength)));
        painter.drawRoundedRect(pill, radius, radius);
        // 3. icon and text, by the delegate, as a plain row
        QStyleOptionViewItem option;
        initViewItemOption(&option);
        option.rect = visualRect(index); // the delegate lays the row out in its full rect (with the title space)
        option.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        option.showDecorationSelected = false;
        if (QAbstractItemDelegate *delegate = itemDelegateForIndex(index)) {
            delegate->paint(&painter, option, index);
        }
        painter.restore();
    }
}

void PlacesPanel::ngApplyPalette()
{
    QPalette glass = m_ngPalette;
    if (ngSetting("SIDEBAR").toLower() != QLatin1String("plain")) {
        // the style highlights the selected and hovered entries with the palette's highlight colour;
        // a translucent white (black on light schemes) turns that into a glass pill over the backdrop
        // the pill itself is painted in paintEvent(); the style must not draw a second one
        glass.setColor(QPalette::Highlight, QColor(255, 255, 255, 0)); // white, not black: the style mixes the rgb with its own alpha
        glass.setColor(QPalette::HighlightedText, glass.color(QPalette::WindowText));
    }
    setPalette(glass);
    viewport()->setPalette(glass);
}

void PlacesPanel::ngApplyDevices(KFilePlacesModel *placesModel)
{
    const bool hide = ngSetting("DEVICES").toLower() != QLatin1String("show") && ngSetting("SIDEBAR").toLower() != QLatin1String("plain");
    for (int row = 0; row < placesModel->rowCount(); ++row) {
        const QModelIndex index = placesModel->index(row, 0);
        if (placesModel->groupType(index) != KFilePlacesModel::DevicesType) {
            continue;
        }
        if (hide) {
            if (!isRowHidden(row)) {
                setRowHidden(row, true);
            }
        } else if (isRowHidden(row) && !placesModel->isHidden(index) && !placesModel->isGroupHidden(KFilePlacesModel::DevicesType)) {
            setRowHidden(row, false);
        }
    }
}

// dolphin-ng: over the glass card the section titles ("Places", "Remote"...) are too faint to read:
// draw each one again as a small dark pill with light text, on top of the original.
void PlacesPanel::paintEvent(QPaintEvent *event)
{
    const bool plain = ngSetting("SIDEBAR").toLower() == QLatin1String("plain");
    // KFilePlacesView shows rows again on its own (delayed updates when devices are found, when the URL changes...):
    // whenever a device row that should be hidden is about to be painted, hide it again
    if (!plain && ngSetting("DEVICES").toLower() != QLatin1String("show") && !m_ngDevicesPending) {
        if (auto *devicesModel = qobject_cast<KFilePlacesModel *>(model())) {
            for (int row = 0; row < devicesModel->rowCount(); ++row) {
                if (!isRowHidden(row) && devicesModel->groupType(devicesModel->index(row, 0)) == KFilePlacesModel::DevicesType) {
                    m_ngDevicesPending = true;
                    QTimer::singleShot(0, this, [this, devicesModel] {
                        ngApplyDevices(devicesModel);
                        m_ngDevicesPending = false;
                    });
                    break;
                }
            }
        }
    }
    KFilePlacesView::paintEvent(event);
    if (!plain) {
        ngPaintPills();
    }
    const auto *placesModel = qobject_cast<KFilePlacesModel *>(model());
    if (plain || !placesModel) {
        return;
    }

    // the first visible row of a group carries the title above it; a row that is not first tells the plain row height
    struct Title {
        QRect rect;
        QString text;
    };
    QList<Title> titles;
    int rowHeight = -1;
    int previousGroup = -1;
    for (int row = 0; row < placesModel->rowCount(); ++row) {
        if (isRowHidden(row)) {
            continue;
        }
        const QModelIndex index = placesModel->index(row, 0);
        const int group = int(placesModel->groupType(index));
        const QRect rect = visualRect(index);
        if (group != previousGroup) {
            titles.append({rect, index.data(KFilePlacesModel::GroupRole).toString()});
        } else if (rowHeight < 0) {
            rowHeight = rect.height();
        }
        previousGroup = group;
    }
    if (titles.isEmpty()) {
        return;
    }

    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 0.84);
    titleFont.setWeight(QFont::DemiBold);
    const QFontMetrics metrics(titleFont);

    // where the titles are, and the pills that replace them
    struct Pill {
        QRect header;
        QRect rect;
        QString text;
    };
    QList<Pill> pills;
    QRegion headers;
    for (const Title &title : std::as_const(titles)) {
        const int headerHeight = rowHeight > 0 ? title.rect.height() - rowHeight : metrics.height() + 10;
        if (headerHeight < metrics.height() || title.text.isEmpty()) {
            continue;
        }
        const QRect header(title.rect.left(), title.rect.top(), title.rect.width(), headerHeight);
        const int pillHeight = qMin(headerHeight - 2, metrics.height() + 8);
        pills.append({header, QRect(header.left() + 2, header.bottom() - pillHeight, metrics.horizontalAdvance(title.text) + 22, pillHeight), title.text});
        headers += header;
    }
    if (pills.isEmpty()) {
        return;
    }

    QPainter painter(viewport());
    // Wipe the original titles: paint the panel's own backdrop (the dock widget draws it) again over the title
    // areas. The list has no background of its own, so this restores exactly what was behind the text.
    if (auto *dock = qobject_cast<DolphinDockWidget *>(parentWidget())) {
        const QPoint offset = viewport()->mapTo(dock, QPoint(0, 0));
        painter.save();
        painter.setClipRegion(headers);
        // with the live blur the glass is translucent: put the window colour back under the title areas first (the
        // old text would show through otherwise), then the glass thins it out as everywhere else
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(headers.boundingRect(), window()->palette().color(QPalette::Window));
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.translate(-offset);
        dock->paintNgGlass(painter);
        painter.restore();
    }

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(titleFont);
    const bool dark = ngIsDark(palette());
    for (const Pill &pill : std::as_const(pills)) {
        // a light glass pill: a faint veil with a hairline edge, and the title in the normal text colour
        const QRectF rect = QRectF(pill.rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(dark ? QColor(255, 255, 255, 46) : QColor(0, 0, 0, 30), 1));
        painter.setBrush(dark ? QColor(255, 255, 255, 30) : QColor(255, 255, 255, 150));
        painter.drawRoundedRect(rect, rect.height() / 2.0, rect.height() / 2.0);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(pill.rect, Qt::AlignCenter, pill.text);
    }
}

void PlacesPanel::setUrl(const QUrl &url)
{
    KFilePlacesView::setUrl(url);
}

QList<QAction *> PlacesPanel::customContextMenuActions() const
{
    return m_customContextMenuActions;
}

void PlacesPanel::setCustomContextMenuActions(const QList<QAction *> &actions)
{
    m_customContextMenuActions = actions;
}

void PlacesPanel::proceedWithTearDown()
{
    if (m_indexToTearDown.isValid()) {
        auto *placesModel = static_cast<KFilePlacesModel *>(model());
        placesModel->requestTeardown(m_indexToTearDown);
    } else {
        qWarning() << "Places entry to tear down is no longer valid";
    }
}

void PlacesPanel::readSettings()
{
    if (GeneralSettings::autoExpandFolders()) {
        setDragAutoActivationDelay(750);
    } else {
        setDragAutoActivationDelay(0);
    }

    const int iconSize = qMax(0, PlacesPanelSettings::iconSize());
    setIconSize(QSize(iconSize, iconSize));
}

static bool isInternalDrag(const QMimeData *mimeData)
{
    const auto formats = mimeData->formats();
    for (const auto &format : formats) {
        // from KFilePlacesModel::_k_internalMimetype
        if (format.startsWith(QLatin1String("application/x-kfileplacesmodel-"))) {
            return true;
        }
    }
    return false;
}

void PlacesPanel::dragMoveEvent(QDragMoveEvent *event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    if (index.isValid()) {
        auto *placesModel = static_cast<KFilePlacesModel *>(model());

        // Reject drag ontop of a non-writable protocol
        // We don't know whether we're dropping inbetween or ontop of a place
        // so still allow internal drag events so that re-arranging still works.
        if (!isInternalDrag(event->mimeData())) {
            const QUrl url = placesModel->url(index);
            if (!url.isValid() || !KProtocolManager::supportsWriting(url)) {
                event->setDropAction(Qt::IgnoreAction);
            } else {
                DragAndDropHelper::updateDropAction(event, url);
            }
        }
    }

    KFilePlacesView::dragMoveEvent(event);
}

void PlacesPanel::slotConfigureTrash()
{
    const QUrl url = currentIndex().data(KFilePlacesModel::UrlRole).toUrl();

    DolphinSettingsDialog *settingsDialog = new DolphinSettingsDialog(url, this);
    settingsDialog->setCurrentPage(settingsDialog->trashSettings);
    settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
    settingsDialog->show();
}

void PlacesPanel::slotUrlsDropped(const QUrl &dest, QDropEvent *event, QWidget *parent)
{
    KIO::DropJob *job = DragAndDropHelper::dropUrls(dest, event, parent);
    if (job) {
        connect(job, &KIO::DropJob::result, this, [this](KJob *job) {
            if (job->error() && job->error() != KIO::ERR_USER_CANCELED) {
                Q_EMIT errorMessage(job->errorString());
            }
        });
    }
}

void PlacesPanel::slotContextMenuAboutToShow(const QModelIndex &index, QMenu *menu)
{
    Q_UNUSED(menu);

    auto *placesModel = static_cast<KFilePlacesModel *>(model());
    const QUrl url = placesModel->url(index);
    const Solid::Device device = placesModel->deviceForIndex(index);

    m_configureTrashAction->setVisible(url.scheme() == QLatin1String("trash"));
    m_openInSplitView->setVisible(url.isValid());

    // show customContextMenuActions only on the view's context menu
    if (!url.isValid() && !device.isValid()) {
        addActions(m_customContextMenuActions);
    } else {
        const auto actions = this->actions();
        for (QAction *action : actions) {
            if (m_customContextMenuActions.contains(action)) {
                removeAction(action);
            }
        }
    }
}

void PlacesPanel::slotTearDownRequested(const QModelIndex &index)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
    if (!storageAccess) {
        return;
    }

    m_indexToTearDown = QPersistentModelIndex(index);

    // disconnect the Solid::StorageAccess::teardownRequested
    // to prevent emitting PlacesPanel::storageTearDownExternallyRequested
    // after we have emitted PlacesPanel::storageTearDownRequested
    disconnect(storageAccess, &Solid::StorageAccess::teardownRequested, this, &PlacesPanel::slotTearDownRequestedExternally);
    Q_EMIT storageTearDownRequested(storageAccess->filePath());
}

void PlacesPanel::slotTearDownRequestedExternally(const QString &udi)
{
    Q_UNUSED(udi);
    auto *storageAccess = static_cast<Solid::StorageAccess *>(sender());

    Q_EMIT storageTearDownExternallyRequested(storageAccess->filePath());
}

void PlacesPanel::slotTearDownDone(const QModelIndex &index, Solid::ErrorType error, const QVariant &errorData)
{
    Q_UNUSED(errorData); // All error handling is currently done in frameworks.

    if (index == m_indexToTearDown) {
        if (error == Solid::ErrorType::NoError) {
            // No error; it must have been unmounted successfully
            Q_EMIT storageTearDownSuccessful();
        }
        m_indexToTearDown = QPersistentModelIndex();
    }
}

void PlacesPanel::slotRowsInserted(const QModelIndex &parent, int first, int last)
{
    for (int i = first; i <= last; ++i) {
        connectDeviceSignals(model()->index(i, 0, parent));
    }
}

void PlacesPanel::slotRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    for (int i = first; i <= last; ++i) {
        const QModelIndex index = placesModel->index(i, 0, parent);

        Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
        if (!storageAccess) {
            continue;
        }

        disconnect(storageAccess, &Solid::StorageAccess::teardownRequested, this, nullptr);
    }
}

void PlacesPanel::connectDeviceSignals(const QModelIndex &index)
{
    auto *placesModel = static_cast<KFilePlacesModel *>(model());

    Solid::StorageAccess *storageAccess = placesModel->deviceForIndex(index).as<Solid::StorageAccess>();
    if (!storageAccess) {
        return;
    }

    connect(storageAccess, &Solid::StorageAccess::teardownRequested, this, &PlacesPanel::slotTearDownRequestedExternally);
}

#include "moc_placespanel.cpp"

// dolphin-ng: the filters section sits at the bottom of the panel, above the disk usage box; the entries end above it
void PlacesPanel::ngSetBottomWidget(QWidget *widget)
{
    m_ngBottom = widget;
    if (widget) {
        widget->setParent(this);
        widget->installEventFilter(this);
        widget->raise();
    }
    ngLayoutBottom();
}

// dolphin-ng: when there are more places below (or above) than fit, the list fades out at that edge instead of
// ending on a row cut in half
namespace
{
// dolphin-ng: with the compositor's blur the glass is see-through, so the list cannot fade "to a colour". This strip
// lies over one end of the list and blends what is under it into the bare glass: the list is thinned out by a
// gradient and the glass ground (the window colour, thinned by the dock exactly as around the list) is added back
// by the same gradient.
class NgListFade : public QWidget
{
public:
    NgListFade(QWidget *parent, bool top)
        : QWidget(parent)
        , m_top(top)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        auto *dock = parentWidget() ? qobject_cast<DolphinDockWidget *>(parentWidget()->parentWidget()) : nullptr;
        if (!dock) {
            return;
        }
        QLinearGradient mask(0, 0, 0, height());
        mask.setColorAt(m_top ? 0.0 : 1.0, QColor(0, 0, 0, 255));
        mask.setColorAt(m_top ? 0.35 : 0.65, QColor(0, 0, 0, 150));
        mask.setColorAt(m_top ? 1.0 : 0.0, QColor(0, 0, 0, 0));
        const qreal dpr = devicePixelRatioF();
        QImage ground((QSizeF(size()) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        ground.setDevicePixelRatio(dpr);
        ground.fill(window()->palette().color(QPalette::Window));
        {
            QPainter painter(&ground);
            painter.save();
            painter.translate(-mapTo(dock, QPoint(0, 0)));
            dock->paintNgGlass(painter);
            painter.restore();
            painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            painter.fillRect(rect(), mask);
        }
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.fillRect(rect(), mask);
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        painter.drawImage(QPointF(0, 0), ground);
    }

private:
    bool m_top;
};
}

void PlacesPanel::ngUpdateFade()
{
    QScrollBar *bar = verticalScrollBar();
    const bool below = bar->value() < bar->maximum();
    const bool above = bar->value() > bar->minimum();
    // (the effect renders the list off screen at every repaint; with the compositor's blur the window is
    // translucent and that is too slow, so there the list simply ends)
    const bool live = ngSetting("GLASS_MODE").toLower() == QLatin1String("live") && ngSetting("SIDEBAR").toLower() != QLatin1String("plain");
    if (live) {
        // two strips over the ends of the list do the fading (cheap: they repaint only their own few rows)
        if (!m_ngFadeTop) {
            m_ngFadeTop = new NgListFade(this, true);
            m_ngFadeBottom = new NgListFade(this, false);
        }
        const QRect list = viewport()->geometry();
        const int strip = qMin(30, list.height() / 3);
        m_ngFadeTop->setGeometry(list.left(), list.top(), list.width(), strip);
        m_ngFadeBottom->setGeometry(list.left(), list.bottom() + 1 - strip, list.width(), strip);
        m_ngFadeTop->setVisible(above);
        m_ngFadeBottom->setVisible(below);
        m_ngFadeTop->raise();
        m_ngFadeBottom->raise();
        if (m_ngOverlayBar) {
            m_ngOverlayBar->raise();
        }
    } else if (m_ngFadeTop) {
        m_ngFadeTop->hide();
        m_ngFadeBottom->hide();
    }
    if ((!below && !above) || live) {
        if (viewport()->graphicsEffect()) {
            viewport()->setGraphicsEffect(nullptr);
        }
        return;
    }
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(viewport()->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(viewport());
        effect->setOpacity(1.0);
        viewport()->setGraphicsEffect(effect);
    }
    const qreal height = qMax(1, viewport()->height());
    const qreal fade = qMin(36.0, height / 3.0) / height;
    QLinearGradient mask(0, 0, 0, height);
    mask.setColorAt(0.0, above ? Qt::transparent : Qt::black);
    mask.setColorAt(above ? fade : 0.0, Qt::black);
    mask.setColorAt(below ? 1.0 - fade : 1.0, Qt::black);
    mask.setColorAt(1.0, below ? Qt::transparent : Qt::black);
    effect->setOpacityMask(mask);
}

// dolphin-ng: Qt's own scroll bar takes a column at the right edge of the panel, outside the glass card. It is
// switched off; a thin bar of ours, tied to it, floats inside the card beside the places.
void PlacesPanel::ngPlaceScrollBar()
{
    QScrollBar *real = verticalScrollBar();
    if (!m_ngOverlayBar) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_ngOverlayBar = new QScrollBar(Qt::Vertical, this);
        m_ngOverlayBar->setStyleSheet(QStringLiteral("QScrollBar:vertical { background: transparent; width: 7px; margin: 0; border: 0; }"
                                                     "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.34); border-radius: 3px; min-height: 28px; }"
                                                     "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.55); }"
                                                     "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                                                     "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
        auto sync = [this, real] {
            m_ngOverlayBar->setRange(real->minimum(), real->maximum());
            m_ngOverlayBar->setPageStep(real->pageStep());
            m_ngOverlayBar->setSingleStep(real->singleStep());
            m_ngOverlayBar->setValue(real->value());
            m_ngOverlayBar->setVisible(real->maximum() > real->minimum());
        };
        connect(real, &QScrollBar::rangeChanged, m_ngOverlayBar, sync);
        connect(real, &QScrollBar::valueChanged, m_ngOverlayBar, &QScrollBar::setValue);
        connect(m_ngOverlayBar, &QScrollBar::valueChanged, real, &QScrollBar::setValue);
        sync();
    }
    const QRect list = viewport()->geometry();
    m_ngOverlayBar->setGeometry(width() - 7 - 5, list.top() + 4, 7, qMax(20, list.height() - 8));
    m_ngOverlayBar->raise();
}

void PlacesPanel::ngLayoutBottom()
{
    if (!m_ngBarWatched) {
        m_ngBarWatched = true;
        verticalScrollBar()->installEventFilter(this);
    }
    if (!m_ngFadeWatched) {
        m_ngFadeWatched = true;
        connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PlacesPanel::ngUpdateFade);
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &PlacesPanel::ngUpdateFade);
    }
    bool shown = m_ngBottom && !m_ngBottom->isHidden();
    if (shown && height() - 12 - 62 - 12 - (m_ngBottom->height() + 8) < 190) {
        // a short window: the places come first, the section steps aside (it is parked below the panel)
        m_ngBottom->move(14, height() + 20);
        shown = false;
    }
    const int extra = shown ? m_ngBottom->height() + 8 : 0;
    setViewportMargins(10, 12, 10, 12 + 62 + extra);
    ngPlaceScrollBar();
    if (shown) {
        // when the places need a scroll bar it runs down the right edge of the whole panel: the section ends
        // beside it instead of under it
        QScrollBar *bar = verticalScrollBar();
        Q_UNUSED(bar)
        const int beside = 0; // the bar floats inside the card, beside the places only
        m_ngBottom->setGeometry(14, height() - 12 - 62 - m_ngBottom->height(), width() - 28 - beside, m_ngBottom->height());
    }
}

bool PlacesPanel::eventFilter(QObject *watched, QEvent *event)
{
    const bool barToggled = m_ngBarWatched && watched == verticalScrollBar() && (event->type() == QEvent::Show || event->type() == QEvent::Hide);
    if (barToggled || (watched == m_ngBottom && (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::Hide))) {
        if (!m_ngLayouting) {
            m_ngLayouting = true;
            ngLayoutBottom();
            m_ngLayouting = false;
        }
    }
    return KFilePlacesView::eventFilter(watched, event);
}

void PlacesPanel::resizeEvent(QResizeEvent *event)
{
    KFilePlacesView::resizeEvent(event);
    ngLayoutBottom();
    ngUpdateFade();
    ngPlaceScrollBar();
}

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
#include <QShowEvent>
#include <QTimer>

#include <Solid/StorageAccess>

PlacesPanel::PlacesPanel(QWidget *parent)
    : KFilePlacesView(parent)
{
    setViewportMargins(10, 12, 10, 12 + 62); // dolphin-neo: keep the entries inside the glass card drawn by the dock, above its disk usage box
    // dolphin-neo: never fill the list background, whatever the style's base colour is, so the glass card shows through
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

    // dolphin-neo: the disk usage box at the bottom of the card replaces the "Devices" section (internal disks);
    // removable devices keep their own section. DOLPHIN_NEO_DEVICES=show keeps the section.
    if (qgetenv("DOLPHIN_NEO_DEVICES").toLower() != "show" && qgetenv("DOLPHIN_NEO_SIDEBAR").toLower() != "plain") {
        // KFilePlacesView decides row visibility itself on model changes, so hide the rows again right after it
        auto hideDevices = [this, placesModel] {
            QTimer::singleShot(0, this, [this, placesModel] {
                for (int row = 0; row < placesModel->rowCount(); ++row) {
                    if (placesModel->groupType(placesModel->index(row, 0)) == KFilePlacesModel::DevicesType && !isRowHidden(row)) {
                        setRowHidden(row, true);
                    }
                }
            });
        };
        connect(placesModel, &QAbstractItemModel::rowsInserted, this, hideDevices);
        connect(placesModel, &QAbstractItemModel::rowsRemoved, this, hideDevices);
        connect(placesModel, &QAbstractItemModel::dataChanged, this, hideDevices);
        connect(placesModel, &QAbstractItemModel::modelReset, this, hideDevices);
        connect(placesModel, &KFilePlacesModel::groupHiddenChanged, this, hideDevices);
        connect(placesModel, &KFilePlacesModel::reloaded, this, hideDevices);
        hideDevices();
    }

    connect(placesModel, &KFilePlacesModel::errorMessage, this, &PlacesPanel::errorMessage);
    connect(placesModel, &KFilePlacesModel::teardownDone, this, &PlacesPanel::slotTearDownDone);

    connect(placesModel, &QAbstractItemModel::rowsInserted, this, &PlacesPanel::slotRowsInserted);
    connect(placesModel, &QAbstractItemModel::rowsAboutToBeRemoved, this, &PlacesPanel::slotRowsAboutToBeRemoved);

    for (int i = 0; i < model()->rowCount(); ++i) {
        connectDeviceSignals(model()->index(i, 0, QModelIndex()));
    }
}

PlacesPanel::~PlacesPanel() = default;

// dolphin-neo: over the glass card the section titles ("Places", "Remote"...) are too faint to read:
// draw each one again as a small dark pill with light text, on top of the original.
void PlacesPanel::paintEvent(QPaintEvent *event)
{
    KFilePlacesView::paintEvent(event);
    static const bool plain = qgetenv("DOLPHIN_NEO_SIDEBAR").toLower() == "plain";
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

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    QFont titleFont = font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 0.84);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    const QFontMetrics metrics(titleFont);
    for (const Title &title : std::as_const(titles)) {
        const int headerHeight = rowHeight > 0 ? title.rect.height() - rowHeight : metrics.height() + 10;
        if (headerHeight < metrics.height() || title.text.isEmpty()) {
            continue;
        }
        const int pillHeight = qMin(headerHeight, metrics.height() + 6);
        const QRect header(title.rect.left(), title.rect.top(), title.rect.width(), headerHeight);
        const QRect pill(header.left() + 2, header.bottom() - pillHeight + 3, metrics.horizontalAdvance(title.text) + 22, pillHeight);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(12, 13, 15, 168));
        painter.drawRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);
        painter.setPen(QColor(236, 239, 244));
        painter.drawText(pill, Qt::AlignCenter, title.text);
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

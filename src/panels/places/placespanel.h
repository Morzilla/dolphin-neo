/*
 * SPDX-FileCopyrightText: 2008-2012 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2010 Christian Muehlhaeuser <muesli@gmail.com>
 * SPDX-FileCopyrightText: 2021 Kai Uwe Broulik <kde@broulik.de>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PLACESPANEL_H
#define PLACESPANEL_H

#include "panels/panel.h"

#include <KFilePlacesView>
#include <QPalette>

class KFilePlacesModel;
#include <QUrl>

#include <Solid/SolidNamespace> // Solid::ErrorType

class QTimer;
namespace Solid
{
class StorageAccess;
}

/**
 * @brief Combines bookmarks and mounted devices as list.
 */
class PlacesPanel : public KFilePlacesView
{
public:
    /** dolphin-ng: a widget kept at the bottom of the panel, above the disk usage box (the filters section). */
    void ngSetBottomWidget(QWidget *widget);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void ngLayoutBottom();
    QWidget *m_ngBottom = nullptr;
    bool m_ngLayouting = false;
    bool m_ngBarWatched = false;

    Q_OBJECT

public:
    explicit PlacesPanel(QWidget *parent);
    ~PlacesPanel() override;

    void setUrl(const QUrl &url); // override

    // for compatibility with Panel, actions that are shown
    // on the view's context menu
    QList<QAction *> customContextMenuActions() const;
    void setCustomContextMenuActions(const QList<QAction *> &actions);

    void requestTearDown();
    void proceedWithTearDown();

public Q_SLOTS:
    void readSettings();

Q_SIGNALS:
    void errorMessage(const QString &error);
    void storageTearDownRequested(const QString &mountPath);
    void storageTearDownExternallyRequested(const QString &mountPath);
    void storageTearDownSuccessful();
    void openInSplitViewRequested(const QUrl &url);

protected:
    void paintEvent(QPaintEvent *event) override; // dolphin-ng: section titles as pills
    void dragMoveEvent(QDragMoveEvent *event) override;

private Q_SLOTS:
    void slotConfigureTrash();
    void slotUrlsDropped(const QUrl &dest, QDropEvent *event, QWidget *parent);
    void slotContextMenuAboutToShow(const QModelIndex &index, QMenu *menu);
    void slotTearDownRequested(const QModelIndex &index);
    void slotTearDownRequestedExternally(const QString &udi);
    void slotTearDownDone(const QModelIndex &index, Solid::ErrorType error, const QVariant &errorData);
    void slotRowsInserted(const QModelIndex &parent, int first, int last);
    void slotRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);

private:
    void ngApplyPalette(); // dolphin-ng: glass highlight
    void ngPaintPills(); // dolphin-ng: glass pill under the selected / hovered entry
    void ngApplyDevices(KFilePlacesModel *placesModel); // dolphin-ng: hide the Devices section unless asked
    QPalette m_ngPalette;
    void connectDeviceSignals(const QModelIndex &idx);

    QList<QAction *> m_customContextMenuActions;

    QPersistentModelIndex m_indexToTearDown;

    std::unique_ptr<QAction> m_configureTrashAction;
    std::unique_ptr<QAction> m_openInSplitView;
    QAction *m_lockPanelsAction;
};

#endif // PLACESPANEL_H

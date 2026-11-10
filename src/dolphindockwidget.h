/*
 * SPDX-FileCopyrightText: 2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHIN_DOCK_WIDGET_H
#define DOLPHIN_DOCK_WIDGET_H

#include <QDockWidget>
#include <QUrl>

class QTimer;

/**
 * @brief Extends QDockWidget to be able to get locked.
 */
class DolphinDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit DolphinDockWidget(const QString &title = QString(), QWidget *parent = nullptr, Qt::WindowFlags flags = {});
    ~DolphinDockWidget() override;

    /**
     * @param lock If \a lock is true, the title bar of the dock-widget will get hidden so
     *             that it is not possible for the user anymore to move or undock the dock-widget.
     */
    void setLocked(bool lock);
    bool isLocked() const;

    /**
     * dolphin-ng: paint the glass card of this panel with \a painter, in this widget's coordinates.
     * Returns false if nothing opaque was painted (stock look, or the "live blur" hole), in which
     * case it cannot be used to wipe something drawn on top of the card.
     */
    bool paintNgGlass(QPainter &painter);
    /** dolphin-ng: true when the sidebar glass is off (SIDEBAR=plain) / when it shows the static image (not GLASS_MODE=live). */
    static bool ngGlassPlain();
    static bool ngGlassImageMode();
    /** dolphin-ng: the disk usage box, in this widget's coordinates (empty when none is drawn), and the mount point it shows. */
    QRectF ngDiskBox() const { return m_ngDiskBox; }
    QString ngDiskRoot() const { return m_ngDiskRoot; }

Q_SIGNALS:
    /** dolphin-ng: the disk usage box was clicked; \a url is the root of that file system. */
    void ngDiskRootRequested(const QUrl &url);

protected:
    void mousePressEvent(QMouseEvent *event) override; // dolphin-ng: disk usage box
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * Make sure we do not emit QDockWidget::visibilityChanged() signals whenever Dolphin's window is minimized or restored.
     */
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override; // dolphin-ng: glass panel
    bool eventFilter(QObject *watched, QEvent *event) override; // dolphin-ng: repaint when the window moves
    void timerEvent(QTimerEvent *event) override; // dolphin-ng: refresh the disk usage box

private:
    /** dolphin-ng: average luminance (0-1) of what lies behind the glass card, or -1 if unknown. */
    qreal backdropLuminance() const;
    /** dolphin-ng: adapt the glass transparency to the backdrop. */
    void updateGlass();

    bool m_locked;
    QWidget *m_dockTitleBar;
    QTimer *m_glassTimer = nullptr;
    QTimer *m_lavaTimer = nullptr; // dolphin-ng: drives the animated ("lava lamp") backdrop
    bool ngDiskBoxEvent(QEvent *event);
    QRectF m_ngDiskBox;
    QString m_ngDiskRoot;
    bool m_ngDiskPressed = false;
    bool m_ngDiskHover = false;
    qreal m_glass = 75; // share (0-100) of the window tint removed under the card
};

#endif

/*
 * SPDX-FileCopyrightText: 2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DOLPHIN_DOCK_WIDGET_H
#define DOLPHIN_DOCK_WIDGET_H

#include <QDockWidget>

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

protected:
    /**
     * Make sure we do not emit QDockWidget::visibilityChanged() signals whenever Dolphin's window is minimized or restored.
     */
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override; // dolphin-neo: glass panel
    bool eventFilter(QObject *watched, QEvent *event) override; // dolphin-neo: repaint when the window moves
    void timerEvent(QTimerEvent *event) override; // dolphin-neo: refresh the disk usage box

private:
    /** dolphin-neo: average luminance (0-1) of what lies behind the glass card, or -1 if unknown. */
    qreal backdropLuminance() const;
    /** dolphin-neo: adapt the glass transparency to the backdrop. */
    void updateGlass();

    bool m_locked;
    QWidget *m_dockTitleBar;
    QTimer *m_glassTimer = nullptr;
    QTimer *m_lavaTimer = nullptr; // dolphin-neo: drives the animated ("lava lamp") backdrop
    qreal m_glass = 75; // share (0-100) of the window tint removed under the card
};

#endif

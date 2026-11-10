/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * Based on the Itemviews NG project from Trolltech Labs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KITEMLISTWIDGET_H
#define KITEMLISTWIDGET_H

#include "dolphin_export.h"
#include "kitemviews/kitemliststyleoption.h"

#include <QBitArray>
#include <QColor>
#include <QPalette>
#include <QGraphicsWidget>
#include <QStyle>
#include <QTimer>

class KItemListSelectionToggle;

/**
 * dolphin-ng settings. A value comes from the environment variable DOLPHIN_NG_<NAME> if it is set,
 * else from dolphinrc, group DolphinNG, key <NAME> (written by the settings page), else it is the fallback.
 */
/**
 * dolphin-ng: emits changed() after the NG settings page applied new values (ngSettingsInvalidate()).
 * Everything that reads a setting at construction or caches it listens here and re-applies it.
 */
class DOLPHIN_EXPORT NgSettings : public QObject
{
    Q_OBJECT
public:
    static NgSettings *instance();
Q_SIGNALS:
    void changed();
};
/** dolphin-ng: forget the cached values, re-read dolphinrc and tell every listener. */
DOLPHIN_EXPORT void ngSettingsInvalidate();
/** dolphin-ng: grows with every ngSettingsInvalidate(), for callers that keep a derived value. */
DOLPHIN_EXPORT int ngSettingsGeneration();
class QDateTime;
/** dolphin-ng: "oggi", "ieri", "7 giorni fa", "2 anni fa". */
DOLPHIN_EXPORT QString ngAgoText(const QDateTime &when);
DOLPHIN_EXPORT QString ngSetting(const char *name, const QString &fallback = QString());
DOLPHIN_EXPORT int ngSettingInt(const char *name, int fallback);
/** Store a value in dolphinrc; an empty value removes it. */
DOLPHIN_EXPORT void setNgSetting(const char *name, const QString &value);
/** True if the environment decides this setting, so the stored value is ignored. */
DOLPHIN_EXPORT bool ngSettingFromEnvironment(const char *name);

/**
 * dolphin-ng: how the row background is drawn in views that use row
 * backgrounds (the details view). Chosen with DOLPHIN_NG_ROWS:
 * card (default), line, stripe (stock Dolphin) or none.
 */
enum class NgRowStyle { Stripe, Card, Line, None, Open };
DOLPHIN_EXPORT NgRowStyle ngRowStyle();
/** dolphin-ng: change the row style now and remember it (dolphinrc, group DolphinNG). */
DOLPHIN_EXPORT void setNgRowStyle(NgRowStyle style);

/**
 * dolphin-ng: the details view shows each item on two lines (name; then the other columns joined,
 * with the date at the right edge) instead of in columns. DOLPHIN_NG_LIST=columns restores the columns.
 */
DOLPHIN_EXPORT bool ngTwoLineList();

/**
 * dolphin-ng: the two-line list has no icons; the file type is a large, faint abbreviation in the row
 * (a watermark), the size has a bar and hovering a row shows its actions. DOLPHIN_NG_ICONS=show keeps the icons.
 */
DOLPHIN_EXPORT bool ngNoIcons();

/** dolphin-ng: is this a dark colour scheme? The fork's own painting picks its shades from the answer. */
DOLPHIN_EXPORT bool ngIsDark(const QPalette &palette);
/** dolphin-ng: \a colour made readable as an accent on the scheme: lightened on dark, darkened on light. */
DOLPHIN_EXPORT QColor ngAccent(const QColor &colour, bool dark);

/** dolphin-ng: in the icons view every item is a dark rounded tile and previews have rounded corners. DOLPHIN_NG_TILES=off disables. */
DOLPHIN_EXPORT bool ngTiles();

/**
 * dolphin-ng: the look of the cards view (DOLPHIN_NG_CARDS = a | b | c):
 *   Tessera  a thumbnail band on top, then the name, chips and a label/value list
 *   Scheda   the icon in a column on the left, three lines of text on the right
 *   Dossier  the list row folded: icon and name, chips, the dates on the left, the big "ago" on the right
 */
enum class NgCardStyle { Tessera, Scheda, Dossier };
DOLPHIN_EXPORT NgCardStyle ngCardStyle();
/** dolphin-ng: cards grow to fill the row (DOLPHIN_NG_CARDS_FIT=stretch) instead of keeping their size. */
DOLPHIN_EXPORT bool ngCardsStretch();

/** dolphin-ng: what replaces the icon — a short type abbreviation, the colour and the category of the file. */
struct NgKind {
    enum Category { Folder, Image, Document, Sheet, Archive, Media, Data, Package, Other, CategoryCount };
    QString sigla;
    QColor tint;
    Category category;
};
DOLPHIN_EXPORT NgKind ngKindFor(const QString &name, bool isDir);
DOLPHIN_EXPORT QColor ngCategoryTint(NgKind::Category category);
class KItemListView;
class QPropertyAnimation;

/**
 * @brief Provides generic information for all KItemListWidgets
 * for which the construction of any specific KItemListWidget isn't required.
 *
 * KItemListView only creates KItemListWidget instances for the visible
 * area. For calculating the required size of all items the expected
 * size for the invisible items must be accessible. KItemListWidgetInformant
 * provides this information.
 */
class DOLPHIN_EXPORT KItemListWidgetInformant
{
public:
    KItemListWidgetInformant();
    virtual ~KItemListWidgetInformant();

    virtual void calculateItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const = 0;

    virtual qreal preferredRoleColumnWidth(const QByteArray &role, int index, const KItemListView *view) const = 0;
};

/**
 * @brief Widget that shows a visible item from the model.
 *
 * For showing an item from a custom model it is required to at least overwrite KItemListWidget::paint().
 * All properties are set by KItemListView, for each property there is a corresponding
 * virtual protected method that allows to react on property changes.
 */
class DOLPHIN_EXPORT KItemListWidget : public QGraphicsWidget
{
    Q_OBJECT

public:
    KItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent);
    ~KItemListWidget() override;

    void setIndex(int index);
    int index() const;

    void setData(const QHash<QByteArray, QVariant> &data, const QSet<QByteArray> &roles = QSet<QByteArray>());
    QHash<QByteArray, QVariant> data() const;
    QVariant value(const QByteArray &key) const;

    /**
     * Draws the hover-rectangle if the item is hovered. Overwrite this method
     * to show the data of the custom model provided by KItemListWidget::data().
     * @reimp
     */
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;

    void setVisibleRoles(const QList<QByteArray> &roles);
    QList<QByteArray> visibleRoles() const;

    /**
     * Sets the width of a role that should be used if the alignment of the content
     * should be done in columns.
     */
    void setColumnWidth(const QByteArray &role, qreal width);
    qreal columnWidth(const QByteArray &role) const;

    void setSidePadding(qreal leftPaddingWidth, qreal rightPaddingWidth);
    qreal leftPadding() const;
    qreal rightPadding() const;

    void setStyleOption(const KItemListStyleOption &option);
    const KItemListStyleOption &styleOption() const;

    // TODO: Hides QGraphicsItem::setSelected()/isSelected(). Replace
    // this by using the default mechanism.
    void setSelected(bool selected);
    bool isSelected() const;

    void setCurrent(bool current);
    bool isCurrent() const;

    void setHovered(bool hovered);
    bool isHovered() const;

    /** Sets a purely visual pressed highlight effect. */
    void setPressed(bool enabled);
    bool isPressed() const;

    void setExpansionAreaHovered(bool hover);
    bool expansionAreaHovered() const;

    void setHoverPosition(const QPointF &pos);

    void setAlternateBackground(bool enable);
    bool alternateBackground() const;

    void setEnabledSelectionToggle(bool enabled);
    bool enabledSelectionToggle() const;

    /**
     * Sets the sibling information for the item and all of its parents.
     * The sibling information of the upper most parent is represented by
     * the first bit, the sibling information of the item by the last bit.
     * The sibling information is useful for drawing the branches in
     * tree views.
     */
    void setSiblingsInformation(const QBitArray &siblings);
    QBitArray siblingsInformation() const;

    /**
     * Allows the user to edit the role \a role. The signals
     * roleEditingCanceled() or roleEditingFinished() will be
     * emitted after editing. An ongoing editing gets canceled if
     * the role is empty. Derived classes must implement
     * editedRoleChanged().
     */
    void setEditedRole(const QByteArray &role);
    QByteArray editedRole() const;

    /**
     * Contains the actual icon size used to draw the icon.
     * Also used during icon resizing animation.
     */
    void setIconSize(int iconSize);
    int iconSize() const;

    /**
     * @return True if \a point is inside KItemListWidget::selectionRectFull(),
     *         KItemListWidget::selectionToggleRect()
     *         or KItemListWidget::expansionToggleRect().
     * @reimp
     */
    bool contains(const QPointF &point) const override;

    /**
     * @return Rectangle for the area that contains the text-properties.
     */
    virtual QRectF textRect() const = 0;

    /**
     * @return Focus rectangle for indicating the current item. Per default
     *         textRect() will be returned. Overwrite this method if textRect()
     *         provides a larger rectangle than the actual text (e.g. to
     *         be aligned with the iconRect()). The textFocusRect() may not be
     *         outside the boundaries of textRect().
     */
    virtual QRectF textFocusRect() const;

    /**
     * Used for drawing the visuals, and situations where we want the behavior of the
     * selection to match the visuals.
     *
     * @return The rectangle around selection.
     */
    virtual QRectF selectionRectFull() const = 0;

    /**
     * @return The core area of the item. All of it reacts exactly the same way to mouse clicks.
     */
    virtual QRectF selectionRectCore() const = 0;

    /**
     * @return Rectangle for the selection-toggle that is used to select or deselect an item.
     *         Per default an empty rectangle is returned which means that no selection-toggle
     *         is available.
     */
    virtual QRectF selectionToggleRect() const;

    /**
     * dolphin-ng: which inline action of the row is at \a pos (in widget coordinates):
     * 0 none, 1 open, 2 menu. The controller handles the click, like for the selection toggle.
     */
    virtual int ngActionAt(const QPointF &pos) const;
    /** dolphin-ng: the NG settings changed; drop whatever layout was derived from them. */
    virtual void ngSettingsChanged();

    /**
     * @return Rectangle for the expansion-toggle that is used to open a sub-tree of the model.
     *         Per default an empty rectangle is returned which means that no opening of sub-trees
     *         is supported.
     */
    virtual QRectF expansionToggleRect() const;

    /**
     * @return Pixmap that is used when dragging an item. Per default the current state of the
     *         widget is returned as pixmap.
     */
    virtual QPixmap createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr);

    /**
     * Starts an animation that makes clear that the item will be activated soon.
     * @param timeUntilActivation time in milliseconds until the item will activate
     */
    virtual void startActivateSoonAnimation(int timeUntilActivation);

Q_SIGNALS:
    void roleEditingCanceled(int index, const QByteArray &role, const QVariant &value);
    void roleEditingFinished(int index, const QByteArray &role, const QVariant &value);

protected:
    virtual void dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles = QSet<QByteArray>());
    virtual void visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous);
    virtual void columnWidthChanged(const QByteArray &role, qreal current, qreal previous);
    virtual void sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth);
    virtual void styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous);
    virtual void currentChanged(bool current);
    virtual void selectedChanged(bool selected);
    virtual void hoveredChanged(bool hovered);
    virtual void pressedChanged(bool pressed);
    virtual void alternateBackgroundChanged(bool enabled);
    virtual void siblingsInformationChanged(const QBitArray &current, const QBitArray &previous);
    virtual void editedRoleChanged(const QByteArray &current, const QByteArray &previous);
    virtual void iconSizeChanged(int current, int previous);
    void resizeEvent(QGraphicsSceneResizeEvent *event) override;
    void clearHoverCache();

    /**
     * Called when the user starts hovering this item.
     */
    virtual void hoverSequenceStarted();

    /**
     * Called in regular intervals while the user is hovering this item.
     *
     * @param sequenceIndex An index that increases over time while the user hovers.
     */
    virtual void hoverSequenceIndexChanged(int sequenceIndex);

    /**
     * Called when the user stops hovering this item.
     */
    virtual void hoverSequenceEnded();

    /**
     * @return The current opacity of the hover-animation. When implementing a custom painting-code for a hover-state
     *         this opacity value should be respected.
     */
    qreal hoverOpacity() const;

    int hoverSequenceIndex() const;

    const KItemListWidgetInformant *informant() const;

private Q_SLOTS:
    void slotHoverSequenceTimerTimeout();

private:
    void initializeSelectionToggle();
    void setHoverOpacity(qreal opacity);
    void drawItemStyleOption(QPainter *painter, QWidget *widget, QStyle::State styleState);

private:
    KItemListWidgetInformant *m_informant;
    int m_index;
    bool m_selected;
    bool m_current;
    bool m_hovered;
    bool m_expansionAreaHovered;
    bool m_alternateBackground;
    bool m_enabledSelectionToggle;
    bool m_clickHighlighted;
    QHash<QByteArray, QVariant> m_data;
    QList<QByteArray> m_visibleRoles;
    QHash<QByteArray, qreal> m_columnWidths;
    qreal m_leftPadding;
    qreal m_rightPadding;
    KItemListStyleOption m_styleOption;
    QBitArray m_siblingsInfo;

    qreal m_hoverOpacity;
    mutable QPixmap *m_hoverCache;

    int m_hoverSequenceIndex;
    QTimer m_hoverSequenceTimer;

    KItemListSelectionToggle *m_selectionToggle;

    QByteArray m_editedRole;
    int m_iconSize;
};

inline const KItemListWidgetInformant *KItemListWidget::informant() const
{
    return m_informant;
}

#endif

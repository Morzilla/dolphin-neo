/*
 * SPDX-FileCopyrightText: 2012 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KSTANDARDITEMLISTWIDGET_H
#define KSTANDARDITEMLISTWIDGET_H

#include "dolphin_export.h"
#include "kitemviews/kitemlistwidget.h"

#include <QPixmap>
#include <QPointer>
#include <QPointF>
#include <QStaticText>

class KItemListRoleEditor;
class KItemListStyleOption;
class KItemListView;
class QVariantAnimation;

/**
 * @brief standard implementation of the ItemList widget informant for use with KStandardItemListView and KStandardItemModel.
 *
 * @see KItemListWidgetInformant
 */
class DOLPHIN_EXPORT KStandardItemListWidgetInformant : public KItemListWidgetInformant
{
public:
    KStandardItemListWidgetInformant();
    ~KStandardItemListWidgetInformant() override;

    void calculateItemSizeHints(QVector<std::pair<qreal /* height */, bool /* isElided */>> &logicalHeightHints,
                                qreal &logicalWidthHint,
                                const KItemListView *view) const override;

    qreal preferredRoleColumnWidth(const QByteArray &role, int index, const KItemListView *view) const override;

protected:
    /**
     * @return The value of the "text" role. The default implementation returns
     *         view->model()->data(index)["text"]. If a derived class can
     *         prevent the (possibly expensive) construction of the
     *         QHash<QByteArray, QVariant> returned by KItemModelBase::data(int),
     *         it can reimplement this function.
     */
    virtual QString itemText(int index, const KItemListView *view) const;

    /**
     * @return The value of the "isLink" role. The default implementation returns false.
     *         The derived class should reimplement this function, when information about
     *         links is available and in usage.
     */
    virtual bool itemIsLink(int index, const KItemListView *view) const;

    /** Configure whether the requested text should be optimized for viewing on a screen or for being read out aloud by a text-to-speech engine. */
    enum class ForUsageAs { DisplayedText, SpokenText };

    /**
     * @param role          The role the text is being requested for.
     * @param values        The data of the item. All the data is passed because the text might depend on multiple data points.
     * @param forUsageAs    Whether the roleText should be optimized for displaying (i.e. kept somewhat short) or optimized for speaking e.g. by screen readers
     *                      or text-to-speech in general (i.e. by preferring announcing a month as July instead of as the number 7).
     * @return String representation of the role \a role. The representation of
     *         a role might depend on other roles, so the values of all roles
     *         are passed as parameter.
     */
    virtual QString roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values, ForUsageAs forUsageAs = ForUsageAs::DisplayedText) const;

    /**
    * @return A font based on baseFont which is customized for symlinks.
    */
    virtual QFont customizedFontForLinks(const QFont &baseFont) const;

    void calculateIconsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const;
    void calculateCompactLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const;
    void calculateDetailsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const;
    void calculateCardsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints, qreal &logicalWidthHint, const KItemListView *view) const; // dolphin-ng

    friend class KStandardItemListWidget; // Accesses roleText()
};

/**
 * @brief standard implementation of an ItemList widget for KStandardItemListView and KStandardItemModel.
 *
 * @see KItemListWidget
 */
class DOLPHIN_EXPORT KStandardItemListWidget : public KItemListWidget
{
    Q_OBJECT

public:
    enum Layout { IconsLayout, CompactLayout, DetailsLayout, CardsLayout /* dolphin-ng */ };

    KStandardItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent);
    ~KStandardItemListWidget() override;

    void setLayout(Layout layout);

    void setHighlightEntireRow(bool highlightEntireRow);
    bool highlightEntireRow() const;

    void setSupportsItemExpanding(bool supportsItemExpanding);
    bool supportsItemExpanding() const;

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;

    QRectF textRect() const override;
    QRectF textFocusRect() const override;
    QRectF selectionRectFull() const override;
    QRectF selectionRectCore() const override;
    QRectF expansionToggleRect() const override;
    QRectF selectionToggleRect() const override;
    int ngActionAt(const QPointF &pos) const override;
    void ngSettingsChanged() override;
    QPixmap createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget = nullptr) override;
    /** @see KItemListWidget::startActivateSoonAnimation() */
    void startActivateSoonAnimation(int timeUntilActivation) override;

    static KItemListWidgetInformant *createInformant();

protected:
    /**
     * Invalidates the cache which results in calling KStandardItemListWidget::refreshCache() as
     * soon as the item need to gets repainted.
     */
    void invalidateCache();

    /**
     * Invalidates the icon cache which results in calling KStandardItemListWidget::refreshCache() as
     * soon as the item needs to get repainted.
     */
    void invalidateIconCache();

    /**
     * Is called if the cache got invalidated by KStandardItemListWidget::invalidateCache().
     * The default implementation is empty.
     */
    virtual void refreshCache();

    /**
     * @return True if the give role should be right aligned when showing it inside a column.
     *         Per default false is returned.
     */
    virtual bool isRoleRightAligned(const QByteArray &role) const;

    /**
     * @return True if the item should be visually marked as hidden item. Per default
     *         false is returned.
     */
    virtual bool isHidden() const;

    /**
     * @return A font based on baseFont which is customized according to the data shown in the widget.
     */
    virtual QFont customizedFont(const QFont &baseFont) const;

    virtual QPalette::ColorRole normalTextColorRole() const;

    void setTextColor(const QColor &color);
    QColor textColor(const QWidget &widget) const;

    void setOverlays(QHash<Qt::Corner, QString> &overlay);
    QHash<Qt::Corner, QString> overlays() const;

    /**
     * @see KStandardItemListWidgetInformant::roleText().
     */
    QString roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values) const;

    static int numberOfUnicodeCharactersIn(const QString &text);

    /**
     * @return Selection length (with or without MIME-type extension) in number of unicode characters, which might be different from number of QChars.
     */
    virtual int selectionLength(const QString &text) const;

    void dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles = QSet<QByteArray>()) override;
    void visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous) override;
    void columnWidthChanged(const QByteArray &role, qreal current, qreal previous) override;
    void sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth) override;
    void styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous) override;
    void hoveredChanged(bool hovered) override;
    void selectedChanged(bool selected) override;
    void pressedChanged(bool pressed) override;
    void siblingsInformationChanged(const QBitArray &current, const QBitArray &previous) override;
    void editedRoleChanged(const QByteArray &current, const QByteArray &previous) override;
    void iconSizeChanged(int current, int previous) override;
    void resizeEvent(QGraphicsSceneResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;

    struct TextInfo {
        QPointF pos;
        QStaticText staticText;
    };
    void updateAdditionalInfoTextColor();

public Q_SLOTS:
    void finishRoleEditing();
    void cancelRoleEditing();
    void updateRoleEditorGeometry();

private Q_SLOTS:
    void slotCutItemsChanged();
    void slotRoleEditingCanceled(const QByteArray &role, const QVariant &value);
    void slotRoleEditingFinished(const QByteArray &role, const QVariant &value);

private:
    void triggerCacheRefreshing();
    void updateExpansionArea();
    void updatePixmapCache();

    void updateTextsCache();
    void updateIconsLayoutTextCache();
    void updateCompactLayoutTextCache();
    void updateDetailsLayoutTextCache();
    // dolphin-ng: the cards view
    void updateCardsLayoutTextCache();
    void ngPlaceCardPixmap();
    void paintNgCardSheet(QPainter *painter);
    bool ngOwnSelection() const override;
    qreal ngSelectionRadius() const override;
    void paintNgCard(QPainter *painter, QWidget *widget);
    qreal ngLayoutActionPill(const QList<NgQuickAction> &quickActions, qreal x, qreal y, qreal right, qreal actionHeight, const QFontMetricsF &chipFm, bool alignRight,
                             const QList<int> &origin = {});
    void paintNgActionPill(QPainter *painter, const QColor &text, bool dark);

    /** Draw the icon overlays at a fixed size in the corners of the on-screen
     *  icon. Must be called after the icon itself has been drawn. */
    void drawOverlays(QPainter *painter) const;

    void drawPixmap(QPainter *painter, const QPixmap &pixmap);
    /** Draw the lines and arrows that visualize the expanded state and level of this row. */
    void drawSiblingsInformation(QPainter *painter);

    QRectF roleEditingRect(const QByteArray &role) const;

    QString elideText(QString text, qreal elidingWidth) const;

    /**
     * Escapes text for display purposes.
     *
     * Replaces '\n' with Unicode line break (U+21B5).
     */
    QString escapeString(const QString &text) const;

    /**
     * Closes the role editor and returns the focus back
     * to the KItemListContainer.
     */
    void closeRoleEditor();

    QPixmap pixmapForIcon(const QString &name, const QSize &size, QIcon::Mode mode) const;

    /**
     * @return Preferred size of the rating-image based on the given
     *         style-option. The height of the font is taken as
     *         reference.
     */
    static QSizeF preferredRatingSize(const KItemListStyleOption &option);

    /**
     * @return Horizontal padding in pixels that is added to the required width of
     *         a column to display the content.
     */
    static qreal columnPadding(const KItemListStyleOption &option);

    /** @returns whether the usual icon should be shown or not. */
    bool isIconControlledByActivateSoonAnimation() const;

protected:
    QHash<QByteArray, TextInfo *> m_textInfo; // PlacesItemListWidget needs to access this

private:
    bool m_isCut;
    bool m_isHidden;
    QFont m_customizedFont;
    QFontMetrics m_customizedFontMetrics;
    bool m_isExpandable;
    bool m_highlightEntireRow;
    bool m_supportsItemExpanding;

    bool m_dirtyLayout;
    bool m_dirtyContent;
    QSet<QByteArray> m_dirtyContentRoles;

    Layout m_layout;
    QPointF m_pixmapPos;
    QPixmap m_pixmap;
    QSize m_scaledPixmapSize; //Size of the pixmap in device independent pixels

    qreal m_columnWidthSum;
    QRectF m_iconRect; // Cache for KItemListWidget::iconRect()

    QRectF m_textRect;

    // dolphin-ng: icon-less row (see ngNoIcons())
    QString m_ngSigla; // file type abbreviation drawn as a watermark
    int m_ngCategory = 0; // NgKind::Category of the file, for the line glyph of the type mark
    QString m_ngIconName; // type icon drawn, faint, before the abbreviation
    QPixmap m_ngIcon;
    bool m_ngIconFaded = false;
    QColor m_ngTint; // colour of the file's category
    QByteArray m_ngDateRole; // the date role shown at the right edge, if any
    QString m_ngAgo; // "3 giorni fa", under the date
    QString m_ngCount; // folders: "20 elementi" under the size of the contents
    struct NgChip {
        QString text;
        QRectF rect;
        bool tinted = false; // the type chip takes the colour of the row
    };
    QList<NgChip> m_ngChips; // second line under the name: type and other details as small pills
    struct NgTimeLine {
        QString letter; // "C" / "M"
        QString text; // "12/10/26 · 29 giorni fa"
        QPointF letterPos;
        QPointF textPos;
    };
    QList<NgTimeLine> m_ngTime; // the time column: creation and modification
    QPointF m_ngCountPos;
    QPointF m_ngAgoPos;
    QRectF m_ngBarRect; // size bar
    qreal m_ngBarFraction = -1;
    QRectF m_ngActionsRect; // where the hover actions go
    QGraphicsWidget *m_ngActions = nullptr;
    // dolphin-ng: the cards view
    struct NgCardText {
        QString text;
        QPointF pos; // baseline origin
        QFont font;
        int tone = 0; // 0 additional-info colour, 1 text colour, 2 dimmer
    };
    QList<NgCardText> m_ngCardTexts;
    QRectF m_ngCardThumb; // the thumbnail band (Tessera) or the icon area
    qreal m_ngCardIconSide = 0; // side of the icon when the pixmap is not a preview
    bool m_ngCardPreview = false; // the pixmap is a preview, not the icon of the type
    bool m_ngCardWatermark = false; // the type is drawn as the background instead of the icon
    QRectF m_ngCardEye; // hover actions: preview and menu
    QRectF m_ngCardMenu;
    QList<QPixmap> m_ngCardActionIcons; // one per entry of m_ngCardActions
    QList<qreal> m_ngCardActionSeps; // x of the hairlines between the groups
    QRectF m_ngCardActionsPill;
    QRectF m_ngCardFab; // Tessera: the main action on its own, astride the bottom edge of the picture band
    bool m_ngCardHoverPill = false; // Tessera: the pill (everything but the main action) shows only while hovered
    QList<int> m_ngCardActionIndex; // one per entry of m_ngCardActions: which quick action of the file it is
    QRectF m_ngRowThumb; // two-line rows: where the small preview is drawn (empty: the type mark is shown)
    QPixmap m_ngRowThumbPixmap;
    qint64 m_ngRowThumbKey = 0;
    QList<NgChip> m_ngCardActions; // quick actions of the file's kind, see ngQuickActionsFor(); clicked through ngActionAt() (10 + n)

    QList<QByteArray> m_sortedVisibleRoles;

    QRectF m_expansionArea;

    QColor m_customTextColor;
    QColor m_additionalInfoTextColor;

    QHash<Qt::Corner, QString> m_overlays;
    QPixmap m_rating;

    KItemListRoleEditor *m_roleEditor;
    KItemListRoleEditor *m_oldRoleEditor;

    /** @see startActivateSoonAnimation() */
    QPointer<QVariantAnimation> m_activateSoonAnimation;

    friend class KStandardItemListWidgetInformant; // Accesses private static methods to be able to
                                                   // share a common layout calculation
};

#endif

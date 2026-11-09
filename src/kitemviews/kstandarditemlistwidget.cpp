/*
 * SPDX-FileCopyrightText: 2012 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kstandarditemlistwidget.h"

#include "dolphin_contentdisplaysettings.h"
#include "kfileitemlistview.h"
#include "kitemlistcontroller.h"
#include "kitemlistselectionmanager.h"
#include "private/kfileitemclipboard.h"
#include "private/kitemlistroleeditor.h"
#include "private/kitemviewsutils.h"
#include "private/kpixmapmodifier.h"

#include <KIconEffect>
#include <KIconLoader>
#include <KIconUtils>
#include <KRatingPainter>
#include <KStandardGuiItem>
#include <KStringHandler>
#include <klocalizedstring.h>

#include <QApplication>
#include <QSet>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QScopeGuard>
#include <QGraphicsSceneResizeEvent>
#include <QGraphicsView>
#include <QPixmapCache>
#include <QStyleOption>
#include <QTextBoundaryFinder>
#include <QVariantAnimation>

#include <cmath>
#include <functional>

namespace
{
// dolphin-neo: the main colour of an icon — the average of its pixels weighted by how saturated they are,
// lightened enough to read on a dark row. Cached per icon name. Invalid if the icon has no usable pixels.
QColor neoIconColor(const QString &iconName, const QPixmap &pixmap)
{
    static QHash<QString, QColor> cache;
    const auto cached = cache.constFind(iconName);
    if (cached != cache.constEnd()) {
        return *cached;
    }
    QColor result;
    if (!pixmap.isNull()) {
        const QImage image = pixmap.toImage().scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
        double red = 0, green = 0, blue = 0, total = 0;
        for (int y = 0; y < image.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = QColor::fromRgba(line[x]);
                if (pixel.alpha() < 96) {
                    continue;
                }
                const double weight = 0.04 + pixel.hsvSaturationF() * pixel.valueF(); // colourful pixels count the most
                red += pixel.redF() * weight;
                green += pixel.greenF() * weight;
                blue += pixel.blueF() * weight;
                total += weight;
            }
        }
        if (total > 0) {
            const QColor average = QColor::fromRgbF(red / total, green / total, blue / total);
            float h, s, l;
            average.getHslF(&h, &s, &l);
            result = QColor::fromHslF(h < 0 ? 0 : h, s, qMax(l, 0.64f));
        }
    }
    cache.insert(iconName, result);
    return result;
}

// dolphin-neo: the two buttons shown on the hovered row, "Open" and a menu with everything else
class NeoRowActions : public QGraphicsWidget
{
public:
    NeoRowActions(QGraphicsItem *parent, std::function<void()> open, std::function<void(const QPoint &)> menu)
        : QGraphicsWidget(parent)
        , m_open(std::move(open))
        , m_menu(std::move(menu))
        , m_openText(KLocalizedString::removeAcceleratorMarker(KStandardGuiItem::open().text()))
    {
        while (m_openText.endsWith(QLatin1Char('.')) || m_openText.endsWith(QChar(0x2026))) {
            m_openText.chop(1); // "Open..." is a dialog caption; here it opens the item directly
        }
        setAcceptedMouseButtons(Qt::NoButton); // drawn here, clicked through KItemListController
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        Q_UNUSED(option)
        const QPalette palette = widget ? widget->palette() : QApplication::palette();
        const QColor text = palette.color(QPalette::WindowText);
        QColor fill = text;
        fill.setAlphaF(0.14);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(openRect(), 8, 8);
        painter->drawRoundedRect(previewRect(), 8, 8);
        painter->drawRoundedRect(menuRect(), 8, 8);
        painter->setPen(text);
        painter->setFont(font());
        painter->drawText(openRect(), Qt::AlignCenter, m_openText);
        painter->setPen(Qt::NoPen);
        painter->setBrush(text);
        for (int i = -1; i <= 1; ++i) {
            painter->drawEllipse(menuRect().center() + QPointF(i * 6.0, 0), 1.7, 1.7);
        }
        // preview: an eye
        const QPointF c = previewRect().center();
        painter->drawEllipse(c, 2.4, 2.4);
        QPainterPath eye;
        eye.moveTo(c + QPointF(-8, 0));
        eye.quadTo(c + QPointF(0, -8), c + QPointF(8, 0));
        eye.quadTo(c + QPointF(0, 8), c + QPointF(-8, 0));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(text, 1.5));
        painter->drawPath(eye);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        event->accept(); // keep the click away from the row underneath
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        event->accept();
        if (openRect().contains(event->pos())) {
            m_open();
        } else if (menuRect().contains(event->pos())) {
            m_menu(event->screenPos());
        }
    }

private:
    QRectF menuRect() const
    {
        const qreal h = size().height();
        return QRectF(size().width() - h, 0, h, h);
    }
    QRectF previewRect() const
    {
        const qreal h = size().height();
        return QRectF(size().width() - 2 * h - 6, 0, h, h);
    }
    QRectF openRect() const
    {
        const qreal h = size().height();
        return QRectF(0, 0, size().width() - 2 * h - 12, h);
    }

    std::function<void()> m_open;
    std::function<void(const QPoint &)> m_menu;
    QString m_openText;
};
}

// #define KSTANDARDITEMLISTWIDGET_DEBUG

KStandardItemListWidgetInformant::KStandardItemListWidgetInformant()
    : KItemListWidgetInformant()
{
}

KStandardItemListWidgetInformant::~KStandardItemListWidgetInformant() = default;

void KStandardItemListWidgetInformant::calculateItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                              qreal &logicalWidthHint,
                                                              const KItemListView *view) const
{
    switch (static_cast<const KStandardItemListView *>(view)->itemLayout()) {
    case KStandardItemListView::IconsLayout:
        calculateIconsLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    case KStandardItemListView::CompactLayout:
        calculateCompactLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    case KStandardItemListView::DetailsLayout:
        calculateDetailsLayoutItemSizeHints(logicalHeightHints, logicalWidthHint, view);
        break;

    default:
        Q_ASSERT(false);
        break;
    }
}

qreal KStandardItemListWidgetInformant::preferredRoleColumnWidth(const QByteArray &role, int index, const KItemListView *view) const
{
    const QHash<QByteArray, QVariant> values = view->model()->data(index);
    const KItemListStyleOption &option = view->styleOption();

    const QString text = roleText(role, values);
    qreal width = KStandardItemListWidget::columnPadding(option);
    if (role != "text") {
        // dolphin-neo: extra room between the columns of the details view (DOLPHIN_NEO_COLGAP, pixels)
        static const int gap = qEnvironmentVariableIsSet("DOLPHIN_NEO_COLGAP") ? qEnvironmentVariableIntValue("DOLPHIN_NEO_COLGAP") : 28;
        width += gap;
    }

    const QFontMetrics &normalFontMetrics = option.fontMetrics;
    const QFontMetrics linkFontMetrics(customizedFontForLinks(option.font));

    if (role == "rating") {
        width += KStandardItemListWidget::preferredRatingSize(option).width();
    } else {
        // If current item is a link, we use the customized link font metrics instead of the normal font metrics.
        const QFontMetrics &fontMetrics = itemIsLink(index, view) ? linkFontMetrics : normalFontMetrics;

        width += fontMetrics.horizontalAdvance(text);

        if (role == "text") {
            if (view->supportsItemExpanding()) {
                // Increase the width by the expansion-toggle and the current expansion level
                const int expandedParentsCount = values.value("expandedParentsCount", 0).toInt();
                const qreal height = option.padding * 2 + qMax(option.iconSize, fontMetrics.height());
                width += (expandedParentsCount + 1) * height;
            }

            // Increase the width by the required space for the icon
            width += option.padding * 2 + option.iconSize;
        }
    }

    return width;
}

QString KStandardItemListWidgetInformant::itemText(int index, const KItemListView *view) const
{
    return view->model()->data(index).value("text").toString();
}

bool KStandardItemListWidgetInformant::itemIsLink(int index, const KItemListView *view) const
{
    Q_UNUSED(index)
    Q_UNUSED(view)
    return false;
}

QString KStandardItemListWidgetInformant::roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values, ForUsageAs forUsageAs) const
{
    if (role == "rating") {
        if (forUsageAs == ForUsageAs::DisplayedText) {
            // Always use an empty text, as the rating is shown by the image m_rating.
            return QString();
        } else {
            const int rating{values.value(role).toInt()};
            // Check if there are half stars
            if (rating % 2) {
                return i18ncp("@accessible rating", "%1 and a half stars", "%1 and a half stars", rating / 2);
            }
            return i18ncp("@accessible rating", "%1 star", "%1 stars", rating / 2);
        }
    }
    return values.value(role).toString();
}

QFont KStandardItemListWidgetInformant::customizedFontForLinks(const QFont &baseFont) const
{
    return baseFont;
}

void KStandardItemListWidgetInformant::calculateIconsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                         qreal &logicalWidthHint,
                                                                         const KItemListView *view) const
{
    const KItemListStyleOption &option = view->styleOption();
    const QFont &normalFont = option.font;
    const int additionalRolesCount = qMax(view->visibleRoles().count() - 1, 0);

    const qreal itemWidth = view->itemSize().width();
    const qreal maxWidth = itemWidth - 2 * option.padding;
    const qreal additionalRolesSpacing = additionalRolesCount * option.fontMetrics.lineSpacing();
    const qreal spacingAndIconHeight = option.iconSize + option.padding * 3;

    const QFont linkFont = customizedFontForLinks(normalFont);

    QTextOption textOption(Qt::AlignHCenter);
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    for (int index = 0; index < logicalHeightHints.count(); ++index) {
        if (logicalHeightHints.at(index).first > 0.0) {
            continue;
        }

        // If the current item is a link, we use the customized link font instead of the normal font.
        const QFont &font = itemIsLink(index, view) ? linkFont : normalFont;

        const QString &text = KStringHandler::preProcessWrap(itemText(index, view));

        // Calculate the number of lines required for wrapping the name
        qreal textHeight = 0;
        QTextLayout layout(text, font);
        layout.setTextOption(textOption);
        layout.beginLayout();
        QTextLine line;
        int lineCount = 0;
        bool isElided = false;
        while ((line = layout.createLine()).isValid()) {
            line.setLineWidth(maxWidth);
            line.naturalTextWidth();
            textHeight += line.height();

            ++lineCount;
            if (lineCount == option.maxTextLines) {
                isElided = layout.createLine().isValid();
                break;
            }
        }
        layout.endLayout();

        // Add one line for each additional information
        textHeight += additionalRolesSpacing;

        logicalHeightHints[index].first = textHeight + spacingAndIconHeight;
        logicalHeightHints[index].second = isElided;
    }

    logicalWidthHint = itemWidth;
}

void KStandardItemListWidgetInformant::calculateCompactLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                           qreal &logicalWidthHint,
                                                                           const KItemListView *view) const
{
    const KItemListStyleOption &option = view->styleOption();
    const QFontMetrics &normalFontMetrics = option.fontMetrics;
    const int additionalRolesCount = qMax(view->visibleRoles().count() - 1, 0);

    const QList<QByteArray> &visibleRoles = view->visibleRoles();
    const bool showOnlyTextRole = (visibleRoles.count() == 1) && (visibleRoles.first() == "text");
    const qreal maxWidth = option.maxTextWidth;
    const qreal paddingAndIconWidth = option.padding * 4 + option.iconSize;
    const qreal height = option.padding * 2 + qMax(option.iconSize, (1 + additionalRolesCount) * normalFontMetrics.lineSpacing());

    const QFontMetrics linkFontMetrics(customizedFontForLinks(option.font));

    for (int index = 0; index < logicalHeightHints.count(); ++index) {
        if (logicalHeightHints.at(index).first > 0.0) {
            continue;
        }

        // If the current item is a link, we use the customized link font metrics instead of the normal font metrics.
        const QFontMetrics &fontMetrics = itemIsLink(index, view) ? linkFontMetrics : normalFontMetrics;

        // For each row exactly one role is shown. Calculate the maximum required width that is necessary
        // to show all roles without horizontal clipping.
        qreal maximumRequiredWidth = 0.0;

        if (showOnlyTextRole) {
            maximumRequiredWidth = fontMetrics.horizontalAdvance(itemText(index, view));
        } else {
            const QHash<QByteArray, QVariant> &values = view->model()->data(index);
            for (const QByteArray &role : visibleRoles) {
                const QString &text = roleText(role, values);
                const qreal requiredWidth = fontMetrics.horizontalAdvance(text);
                maximumRequiredWidth = qMax(maximumRequiredWidth, requiredWidth);
            }
        }

        qreal width = paddingAndIconWidth + maximumRequiredWidth;
        if (maxWidth > 0 && width > maxWidth) {
            width = maxWidth;
        }

        logicalHeightHints[index].first = width;
    }

    logicalWidthHint = height;
}

void KStandardItemListWidgetInformant::calculateDetailsLayoutItemSizeHints(QVector<std::pair<qreal, bool>> &logicalHeightHints,
                                                                           qreal &logicalWidthHint,
                                                                           const KItemListView *view) const
{
    const KItemListStyleOption &option = view->styleOption();
    // dolphin-neo: two text lines per row
    const int textHeight = neoTwoLineList() ? 2 * option.fontMetrics.height() + 4 : option.fontMetrics.height();
    const qreal height = neoNoIcons() ? option.padding * 2 + textHeight + 24 // no icon: the row is as tall as its two lines of text
                                      : option.padding * 2 + qMax(option.iconSize, textHeight);
    logicalHeightHints.fill(std::make_pair(height, false));
    logicalWidthHint = -1.0;
}

KStandardItemListWidget::KStandardItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
    : KItemListWidget(informant, parent)
    , m_textInfo()
    , m_isCut(false)
    , m_isHidden(false)
    , m_customizedFont()
    , m_customizedFontMetrics(m_customizedFont)
    , m_isExpandable(false)
    , m_highlightEntireRow(false)
    , m_supportsItemExpanding(false)
    , m_dirtyLayout(true)
    , m_dirtyContent(true)
    , m_dirtyContentRoles()
    , m_layout(IconsLayout)
    , m_pixmapPos()
    , m_pixmap()
    , m_scaledPixmapSize()
    , m_columnWidthSum()
    , m_iconRect()
    , m_textRect()
    , m_sortedVisibleRoles()
    , m_expansionArea()
    , m_customTextColor()
    , m_additionalInfoTextColor()
    , m_overlays()
    , m_rating()
    , m_roleEditor(nullptr)
    , m_oldRoleEditor(nullptr)
{
}

KStandardItemListWidget::~KStandardItemListWidget()
{
    qDeleteAll(m_textInfo);
    m_textInfo.clear();

    if (m_roleEditor) {
        m_roleEditor->deleteLater();
    }

    if (m_oldRoleEditor) {
        m_oldRoleEditor->deleteLater();
    }
}

void KStandardItemListWidget::setLayout(Layout layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        m_dirtyLayout = true;
        updateAdditionalInfoTextColor();
        update();
    }
}

void KStandardItemListWidget::setHighlightEntireRow(bool highlightEntireRow)
{
    if (m_highlightEntireRow != highlightEntireRow) {
        m_highlightEntireRow = highlightEntireRow;
        m_dirtyLayout = true;
        update();
    }
}

bool KStandardItemListWidget::highlightEntireRow() const
{
    return m_highlightEntireRow;
}

void KStandardItemListWidget::setSupportsItemExpanding(bool supportsItemExpanding)
{
    if (m_supportsItemExpanding != supportsItemExpanding) {
        m_supportsItemExpanding = supportsItemExpanding;
        m_dirtyLayout = true;
        update();
    }
}

bool KStandardItemListWidget::supportsItemExpanding() const
{
    return m_supportsItemExpanding;
}

void KStandardItemListWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();

    KItemListWidget::paint(painter, option, widget);

    if (m_layout == IconsLayout && neoTiles()) {
        // dolphin-neo: a dark rounded tile with a hairline of light on top, like the cards of the list
        const QRectF tile = rect().adjusted(3, 3, -3, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 62));
        painter->drawRoundedRect(tile, 12, 12);
        painter->setPen(QPen(QColor(255, 255, 255, 11), 1));
        painter->drawLine(QPointF(tile.left() + 12, tile.top() + 0.5), QPointF(tile.right() - 12, tile.top() + 0.5));
        painter->restore();
    }

    const bool neoRow = m_layout == DetailsLayout && neoNoIcons();
    if (neoRow && !m_neoSigla.isEmpty()) {
        // the file type as a watermark: large, faint letters at the start of the row, fading out under the name
        const bool openRows = neoRowStyle() == NeoRowStyle::Open;
        const QRectF rowRect = openRows ? selectionRectFull() : selectionRectFull().adjusted(3, 3, -3, -3);
        qreal x0 = rowRect.left() + (openRows ? 16 : 10);
        QFont watermarkFont(m_customizedFont);
        watermarkFont.setPixelSize(qMax(12, qRound(size().height() * (neoRowStyle() == NeoRowStyle::Open ? 0.58 : 0.50))));
        watermarkFont.setWeight(QFont::Black);
        watermarkFont.setLetterSpacing(QFont::PercentageSpacing, 94);
        const qreal fade = QFontMetricsF(watermarkFont).horizontalAdvance(m_neoSigla) + m_customizedFontMetrics.height() * 1.5;
        QPainterPath clip;
        clip.addRoundedRect(rowRect, openRows ? 0 : 5, openRows ? 0 : 5);
        painter->save();
        painter->setClipPath(clip, Qt::IntersectClip);

        // the row takes the colour of the file's category, strongest at the start and gone by the middle
        QLinearGradient wash(rowRect.left(), 0, rowRect.left() + rowRect.width() * (openRows ? 0.40 : 0.62), 0);
        QColor washStart = m_neoTint, washMid = m_neoTint, washEnd = m_neoTint;
        washStart.setAlphaF(openRows ? 0.14 : 0.22);
        washMid.setAlphaF(openRows ? 0.05 : 0.06);
        washEnd.setAlphaF(0.0);
        wash.setColorAt(0.0, washStart);
        wash.setColorAt(0.55, washMid);
        wash.setColorAt(1.0, washEnd);
        painter->fillRect(rowRect, wash);

        // the type icon, faint, then the abbreviation
        if (!m_neoIcon.isNull() && !openRows) {
            const qreal iconSide = m_neoIcon.width() / m_neoIcon.devicePixelRatio();
            painter->setOpacity(0.38);
            painter->drawPixmap(QPointF(x0, (size().height() - iconSide) / 2.0), m_neoIcon);
            painter->setOpacity(1.0);
            x0 += iconSide + 8;
        }
        // The abbreviation. DOLPHIN_NEO_SIGLA picks how it is drawn:
        //   fill (default)     large solid letters in the row colour, faint
        //   outline            hollow letters: a thin outline that thins out to the right, barely filled
        //   label              small, widely spaced capitals in the full row colour
        static const QByteArray siglaStyle = qgetenv("DOLPHIN_NEO_SIGLA").toLower();
        const qreal rowHeight = size().height();
        if (siglaStyle == "label") {
            QFont labelFont(m_customizedFont);
            labelFont.setPixelSize(qMax(9, qRound(m_customizedFontMetrics.height() * 0.66)));
            labelFont.setWeight(QFont::DemiBold);
            labelFont.setLetterSpacing(QFont::PercentageSpacing, 118);
            QColor ink = m_neoTint;
            ink.setAlphaF(0.92);
            painter->setFont(labelFont);
            painter->setPen(ink);
            painter->drawText(QRectF(x0, 0, fade * 2, rowHeight), Qt::AlignLeft | Qt::AlignVCenter, m_neoSigla);
        } else if (siglaStyle != "outline") {
            QColor ink = m_neoTint;
            ink.setAlphaF(0.36);
            painter->setFont(watermarkFont);
            painter->setPen(ink);
            painter->drawText(QRectF(x0, -rowHeight, fade * 2, 3 * rowHeight), Qt::AlignLeft | Qt::AlignVCenter, m_neoSigla);
        } else {
            const QFontMetricsF metrics(watermarkFont);
            QPainterPath letters;
            letters.addText(QPointF(x0, (rowHeight + metrics.capHeight()) / 2.0), watermarkFont, m_neoSigla);
            QLinearGradient outline(x0, 0, x0 + fade, 0);
            QColor strong = m_neoTint, weak = m_neoTint, body = m_neoTint;
            strong.setAlphaF(0.80);
            weak.setAlphaF(0.28);
            body.setAlphaF(0.07);
            outline.setColorAt(0.0, strong);
            outline.setColorAt(1.0, weak);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->fillPath(letters, body);
            painter->strokePath(letters, QPen(QBrush(outline), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        painter->restore();

        if (m_neoBarFraction >= 0) {
            QColor groove = styleOption().palette.color(QPalette::Text);
            groove.setAlphaF(0.10);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(groove);
            painter->drawRoundedRect(m_neoBarRect, 2, 2);
            painter->setBrush(m_neoTint);
            const qreal filled = qMax(qreal(4), m_neoBarRect.width() * m_neoBarFraction);
            painter->drawRoundedRect(QRectF(m_neoBarRect.right() - filled, m_neoBarRect.top(), filled, m_neoBarRect.height()), 2, 2);
            painter->restore();
        }
    }

    if (!m_expansionArea.isEmpty()) {
        drawSiblingsInformation(painter);
    }

    // Overlays are not baked into the icon pixmap: it gets rescaled to its final
    // size in drawPixmap(), which would rescale them too. They are drawn at a
    // fixed size by drawOverlays() below. See bug 498211.
    const auto pixmap = neoRow ? QPixmap() : m_pixmap; // dolphin-neo: no icon in the icon-less row

    const KItemListStyleOption &itemListStyleOption = styleOption();
    if (isHovered() && !pixmap.isNull()) {
        if (hoverOpacity() < 1.0) {
            /*
             * Linear interpolation between pixmap and m_hoverPixmap.
             *
             * Note that this cannot be achieved by painting m_hoverPixmap over
             * pixmap, even if the opacities are adjusted. For details see
             * https://git.reviewboard.kde.org/r/109614/
             */
            // Paint pixmap1 so that pixmap1 = pixmap * (1.0 - hoverOpacity())
            QPixmap pixmap1(pixmap.size());
            pixmap1.setDevicePixelRatio(pixmap.devicePixelRatio());
            pixmap1.fill(Qt::transparent);
            {
                QPainter p(&pixmap1);
                p.setOpacity(1.0 - hoverOpacity());
                p.drawPixmap(0, 0, pixmap);
            }

            // Paint pixmap2 so that pixmap2 = m_hoverPixmap * hoverOpacity()
            QPixmap pixmap2(pixmap1.size());
            pixmap2.setDevicePixelRatio(pixmap1.devicePixelRatio());
            pixmap2.fill(Qt::transparent);
            {
                QPainter p(&pixmap2);
                p.setOpacity(hoverOpacity());
                p.drawPixmap(0, 0, m_pixmap);
            }

            // Paint pixmap2 on pixmap1 using CompositionMode_Plus
            // Now pixmap1 = pixmap2 + pixmap * (1.0 - hoverOpacity())
            //             = m_hoverPixmap * hoverOpacity() + pixmap * (1.0 - hoverOpacity())
            {
                QPainter p(&pixmap1);
                p.setCompositionMode(QPainter::CompositionMode_Plus);
                p.drawPixmap(0, 0, pixmap2);
            }

            // Finally paint pixmap1 on the widget
            drawPixmap(painter, pixmap1);
        } else {
            drawPixmap(painter, pixmap);
        }
    } else if (!pixmap.isNull()) {
        drawPixmap(painter, pixmap);
    }

    if (!m_overlays.isEmpty() && !pixmap.isNull()) {
        drawOverlays(painter);
    }

    painter->setFont(m_customizedFont);
    painter->setPen(textColor(*widget));
    const TextInfo *textInfo = m_textInfo.value("text");

    if (!textInfo) {
        // It seems that we can end up here even if m_textInfo does not contain
        // the key "text", see bug 306167. According to triggerCacheRefreshing(),
        // this can only happen if the index is negative. This can happen when
        // the item is about to be removed, see KItemListView::slotItemsRemoved().
        // TODO: try to reproduce the crash and find a better fix.
        return;
    }

    painter->drawStaticText(textInfo->pos, textInfo->staticText);

    bool clipAdditionalInfoBounds = false;
    if (m_supportsItemExpanding && m_sortedVisibleRoles.count() > 1 && !(m_layout == DetailsLayout && neoTwoLineList())) {
        // Prevent a possible overlapping of the additional-information-texts with the icon.
        // This will happen if the user has resized the width of the name-column to be very narrow while having folders expanded.
        // We only want to draw additional info text outside the area of the icon or expansion area, so we set a clip rect that does not contain the icon area.
        // This needs to work both for left-to-right as well as right-to-left layout directions.
        const TextInfo *potentiallyOverlappingRoleText = m_textInfo.value(m_sortedVisibleRoles[1]); // Only the first column after the name column can overlap.
        if (layoutDirection() == Qt::LeftToRight) { // In left-to-right languages the left end of text would overlap. This is mirrored for right-to-left.
            const qreal minX = m_iconRect.right() + 2 * itemListStyleOption.padding;
            if (potentiallyOverlappingRoleText->pos.x() < minX) {
                clipAdditionalInfoBounds = true;
                painter->save();
                painter->setClipRect(minX, 0, size().width() - minX, size().height(), Qt::IntersectClip);
            }
        } else {
            const qreal maxX = m_iconRect.left() - 2 * itemListStyleOption.padding;
            if (potentiallyOverlappingRoleText->pos.x() + m_customizedFontMetrics.horizontalAdvance(potentiallyOverlappingRoleText->staticText.text()) > maxX) {
                clipAdditionalInfoBounds = true;
                painter->save();
                painter->setClipRect(0, 0, maxX, size().height(), Qt::IntersectClip);
            }
        }
    }

    painter->setPen(m_additionalInfoTextColor);
    painter->setFont(m_customizedFont);

    const bool neoActionsShown = neoRow && m_neoActions && m_neoActions->isVisible();
    for (int i = 1; i < m_sortedVisibleRoles.count(); ++i) {
        if (neoActionsShown && m_sortedVisibleRoles[i] == m_neoDateRole) {
            continue; // dolphin-neo: the hover actions take the place of the date
        }
        const TextInfo *textInfo = m_textInfo.value(m_sortedVisibleRoles[i]);
        painter->drawStaticText(textInfo->pos, textInfo->staticText);
    }

    if (!m_rating.isNull()) {
        const TextInfo *ratingTextInfo = m_textInfo.value("rating");
        QPointF pos = ratingTextInfo->pos;
        const Qt::Alignment align = ratingTextInfo->staticText.textOption().alignment();
        if (align & Qt::AlignHCenter) {
            pos.rx() += (size().width() - m_rating.width() / m_rating.devicePixelRatioF()) / 2 - 2;
        }
        painter->drawPixmap(pos, m_rating);
    }

    if (clipAdditionalInfoBounds) {
        painter->restore();
    }

#ifdef KSTANDARDITEMLISTWIDGET_DEBUG
    painter->setBrush(Qt::NoBrush);
    painter->setPen(Qt::green);
    painter->drawRect(m_iconRect);

    painter->setPen(Qt::blue);
    painter->drawRect(m_textRect);

    painter->setPen(Qt::red);
    painter->drawText(QPointF(0, m_customizedFontMetrics.height()), QString::number(index()));
    painter->drawRect(rect());
#endif
}

QRectF KStandardItemListWidget::textRect() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    return m_textRect;
}

QRectF KStandardItemListWidget::textFocusRect() const
{
    // In the compact- and details-layout a larger textRect() is returned to be aligned
    // with the iconRect(). This is useful to have a larger selection/hover-area
    // when having a quite large icon size but only one line of text. Still the
    // focus rectangle should be shown as narrow as possible around the text.

    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();

    switch (m_layout) {
    case CompactLayout: {
        QRectF rect = m_textRect;
        const TextInfo *topText = m_textInfo.value(m_sortedVisibleRoles.first());
        const TextInfo *bottomText = m_textInfo.value(m_sortedVisibleRoles.last());
        rect.setTop(topText->pos.y());
        rect.setBottom(bottomText->pos.y() + bottomText->staticText.size().height());
        return rect;
    }

    case DetailsLayout: {
        QRectF rect = m_textRect;
        const TextInfo *textInfo = m_textInfo.value(m_sortedVisibleRoles.first());
        rect.setTop(textInfo->pos.y());
        rect.setBottom(textInfo->pos.y() + textInfo->staticText.size().height());

        const KItemListStyleOption &option = styleOption();
        if (option.extendedSelectionRegion) {
            const QString text = textInfo->staticText.text();
            rect.setWidth(m_customizedFontMetrics.horizontalAdvance(text) + 2 * option.padding);
        }

        return rect;
    }

    default:
        break;
    }

    return m_textRect;
}

int KStandardItemListWidget::neoActionAt(const QPointF &pos) const
{
    if (!m_neoActions || !m_neoActions->isVisible() || !m_neoActionsRect.contains(pos)) {
        return 0;
    }
    // same split as NeoRowActions: "Open", then two square buttons: preview and menu
    const qreal side = m_neoActionsRect.height();
    const qreal menuLeft = m_neoActionsRect.right() - side;
    const qreal previewLeft = menuLeft - 6 - side;
    if (pos.x() >= menuLeft) {
        return 2;
    }
    if (pos.x() >= previewLeft && pos.x() <= menuLeft - 6) {
        return 3;
    }
    return pos.x() <= previewLeft - 6 ? 1 : 0;
}

QRectF KStandardItemListWidget::selectionRectFull() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    const int padding = styleOption().padding;
    if (m_layout == DetailsLayout && neoTwoLineList()) {
        // dolphin-neo: there are no columns, the whole row is the item
        return QRectF(leftPadding(), 0, size().width() - leftPadding(), size().height());
    }
    if (m_layout == DetailsLayout) {
        auto rect = m_iconRect | m_textRect;
        if (m_highlightEntireRow) {
            if (layoutDirection() == Qt::LeftToRight) {
                rect.setRight(leftPadding() + m_columnWidthSum);
            } else {
                rect.setLeft(size().width() - m_columnWidthSum - rightPadding());
            }
        }
        return rect.adjusted(-padding, 0, padding, 0);
    } else {
        if (m_layout == CompactLayout) {
            return rect().adjusted(-padding, 0, padding, 0);
        }
        return rect();
    }
}

QRectF KStandardItemListWidget::selectionRectCore() const
{
    // Allow dragging from selection area in details view.
    if (m_layout == DetailsLayout && highlightEntireRow() && !isSelected()) {
        QRectF result = m_iconRect | m_textRect;
        return result;
    }
    return selectionRectFull();
}

QRectF KStandardItemListWidget::expansionToggleRect() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();
    return m_isExpandable ? m_expansionArea : QRectF();
}

QRectF KStandardItemListWidget::selectionToggleRect() const
{
    const_cast<KStandardItemListWidget *>(this)->triggerCacheRefreshing();

    const int widgetIconSize = styleOption().iconSize;
    int toggleSize = KIconLoader::SizeSmall;
    if (widgetIconSize >= KIconLoader::SizeEnormous) {
        toggleSize = KIconLoader::SizeMedium;
    } else if (widgetIconSize >= KIconLoader::SizeLarge) {
        toggleSize = KIconLoader::SizeSmallMedium;
    }

    const int padding = styleOption().padding;
    const QRectF selectionRectMinusPadding = selectionRectFull().adjusted(padding, padding, -padding, -padding);
    QPointF pos = selectionRectMinusPadding.topLeft();
    if (QApplication::isRightToLeft()) {
        pos.setX(selectionRectMinusPadding.right() - (pos.x() + toggleSize - selectionRectMinusPadding.left()));
    }

    return QRectF(pos, QSizeF(toggleSize, toggleSize));
}

QPixmap KStandardItemListWidget::createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QPixmap pixmap = KItemListWidget::createDragPixmap(option, widget);
    if (m_layout != DetailsLayout) {
        return pixmap;
    }

    // Only return the content of the text-column as pixmap
    const int leftClip = m_pixmapPos.x();

    const TextInfo *textInfo = m_textInfo.value("text");
    const int rightClip = textInfo->pos.x() + textInfo->staticText.size().width() + 2 * styleOption().padding;

    QPixmap clippedPixmap(rightClip - leftClip + 1, pixmap.height());
    clippedPixmap.fill(Qt::transparent);

    QPainter painter(&clippedPixmap);
    painter.drawPixmap(-leftClip, 0, pixmap);

    return clippedPixmap;
}

void KStandardItemListWidget::startActivateSoonAnimation(int timeUntilActivation)
{
    if (m_activateSoonAnimation) {
        m_activateSoonAnimation->stop(); // automatically DeleteWhenStopped
    }

    m_activateSoonAnimation = new QVariantAnimation{this};
    m_activateSoonAnimation->setStartValue(0.0);
    m_activateSoonAnimation->setEndValue(1.0);
    m_activateSoonAnimation->setDuration(timeUntilActivation);

    const QVariant originalIconName{value("iconName")};
    connect(m_activateSoonAnimation, &QVariantAnimation::valueChanged, this, [originalIconName, this](const QVariant &value) {
        auto progress = value.toFloat();

        QVariant wantedIconName;
        if (progress < 0.333) {
            wantedIconName = "folder-open";
        } else if (progress < 0.666) {
            wantedIconName = originalIconName;
        } else {
            wantedIconName = "folder-open";
        }

        QHash<QByteArray, QVariant> itemData{data()};
        if (itemData["iconName"] != wantedIconName) {
            itemData.insert("iconName", wantedIconName);
            setData(itemData);
            invalidateIconCache();
        }
    });

    connect(m_activateSoonAnimation, &QObject::destroyed, this, [originalIconName, this]() {
        QHash<QByteArray, QVariant> itemData{data()};
        if (itemData["iconName"] == "folder-open") {
            itemData.insert("iconName", originalIconName);
            setData(itemData);
            invalidateIconCache();
        }
    });

    m_activateSoonAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

bool KStandardItemListWidget::isIconControlledByActivateSoonAnimation() const
{
    return m_activateSoonAnimation && value("iconName") == "folder-open";
}

KItemListWidgetInformant *KStandardItemListWidget::createInformant()
{
    return new KStandardItemListWidgetInformant();
}

void KStandardItemListWidget::invalidateCache()
{
    m_dirtyLayout = true;
    m_dirtyContent = true;
}

void KStandardItemListWidget::invalidateIconCache()
{
    m_dirtyContent = true;
    m_dirtyContentRoles.insert("iconPixmap");
}

void KStandardItemListWidget::refreshCache()
{
}

bool KStandardItemListWidget::isRoleRightAligned(const QByteArray &role) const
{
    Q_UNUSED(role)
    return false;
}

bool KStandardItemListWidget::isHidden() const
{
    return false;
}

QFont KStandardItemListWidget::customizedFont(const QFont &baseFont) const
{
    return baseFont;
}

QPalette::ColorRole KStandardItemListWidget::normalTextColorRole() const
{
    if (isPressed()) {
        return QPalette::HighlightedText;
    } else {
        return QPalette::Text;
    }
}

void KStandardItemListWidget::setTextColor(const QColor &color)
{
    if (color != m_customTextColor) {
        m_customTextColor = color;
        updateAdditionalInfoTextColor();
        update();
    }
}

QColor KStandardItemListWidget::textColor(const QWidget &widget) const
{
    if (!isSelected()) {
        if (m_isHidden) {
            return m_additionalInfoTextColor;
        } else if (m_customTextColor.isValid()) {
            return m_customTextColor;
        }
    }

    const QPalette::ColorGroup group = isActiveWindow() && widget.hasFocus() ? QPalette::Active : QPalette::Inactive;
    const QPalette::ColorRole role = normalTextColorRole();
    return styleOption().palette.color(group, role);
}

void KStandardItemListWidget::setOverlays(QHash<Qt::Corner, QString> &overlays)
{
    if (overlays == m_overlays) {
        return;
    }

    m_overlays = overlays;
    m_dirtyContent = true;
    m_dirtyContentRoles.insert("iconOverlays");
    update();
}

QHash<Qt::Corner, QString> KStandardItemListWidget::overlays() const
{
    return m_overlays;
}

QString KStandardItemListWidget::roleText(const QByteArray &role, const QHash<QByteArray, QVariant> &values) const
{
    return static_cast<const KStandardItemListWidgetInformant *>(informant())->roleText(role, values);
}

void KStandardItemListWidget::dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles)
{
    Q_UNUSED(current)

    m_dirtyContent = true;

    QSet<QByteArray> dirtyRoles;
    if (roles.isEmpty()) {
        const auto visibleRoles = this->visibleRoles();
        dirtyRoles = QSet<QByteArray>(visibleRoles.constBegin(), visibleRoles.constEnd());
    } else {
        dirtyRoles = roles;
    }

    // The URL might have changed (i.e., if the sort order of the items has
    // been changed). Therefore, the "is cut" state must be updated.
    KFileItemClipboard *clipboard = KFileItemClipboard::instance();
    const QUrl itemUrl = data().value("url").toUrl();
    m_isCut = clipboard->isCut(itemUrl);

    // The icon-state might depend from other roles and hence is
    // marked as dirty whenever a role has been changed
    dirtyRoles.insert("iconPixmap");
    dirtyRoles.insert("iconName");

    QSetIterator<QByteArray> it(dirtyRoles);
    while (it.hasNext()) {
        const QByteArray &role = it.next();
        m_dirtyContentRoles.insert(role);
    }
}

void KStandardItemListWidget::visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous)
{
    Q_UNUSED(previous)
    m_sortedVisibleRoles = current;
    m_dirtyLayout = true;
}

void KStandardItemListWidget::columnWidthChanged(const QByteArray &role, qreal current, qreal previous)
{
    Q_UNUSED(role)
    Q_UNUSED(current)
    Q_UNUSED(previous)
    m_dirtyLayout = true;
}

void KStandardItemListWidget::sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth)
{
    Q_UNUSED(leftPaddingWidth)
    Q_UNUSED(rightPaddingWidth)
    m_dirtyLayout = true;
}

void KStandardItemListWidget::styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous)
{
    KItemListWidget::styleOptionChanged(current, previous);

    updateAdditionalInfoTextColor();
    m_dirtyLayout = true;
}

void KStandardItemListWidget::hoveredChanged(bool hovered)
{
    if (!hovered && m_activateSoonAnimation) {
        m_activateSoonAnimation->stop(); // automatically DeleteWhenStopped
    }
    m_dirtyLayout = true;

    // dolphin-neo: actions of the hovered row
    if (m_layout == DetailsLayout && neoNoIcons()) {
        if (hovered) {
            if (!m_neoActions) {
                auto controllerOf = [this]() -> KItemListController * {
                    const auto *view = dynamic_cast<KItemListView *>(parentItem());
                    return view ? view->controller() : nullptr;
                };
                m_neoActions = new NeoRowActions(
                    this,
                    [this, controllerOf] {
                        if (KItemListController *controller = controllerOf()) {
                            Q_EMIT controller->itemActivated(index());
                        }
                    },
                    [this, controllerOf](const QPoint &screenPos) {
                        if (KItemListController *controller = controllerOf()) {
                            // the context menu acts on the selection: make it this item
                            controller->selectionManager()->clearSelection();
                            controller->selectionManager()->setSelected(index(), 1);
                            controller->selectionManager()->setCurrentItem(index());
                            Q_EMIT controller->itemContextMenuRequested(index(), QPointF(screenPos));
                        }
                    });
                m_neoActions->setFont(m_customizedFont);
            }
            triggerCacheRefreshing();
            m_neoActions->setGeometry(m_neoActionsRect);
            m_neoActions->setVisible(!m_neoActionsRect.isEmpty());
        } else if (m_neoActions) {
            m_neoActions->hide();
        }
        update();
    }
}

void KStandardItemListWidget::selectedChanged(bool selected)
{
    Q_UNUSED(selected)
    updateAdditionalInfoTextColor();
    m_dirtyContent = true;
}

void KStandardItemListWidget::pressedChanged(bool pressed)
{
    Q_UNUSED(pressed)
    updateAdditionalInfoTextColor();
    m_dirtyContent = true;
}

void KStandardItemListWidget::siblingsInformationChanged(const QBitArray &current, const QBitArray &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
    m_dirtyLayout = true;
}

int KStandardItemListWidget::numberOfUnicodeCharactersIn(const QString &text)
{
    int count = 0;
    QTextBoundaryFinder boundaryFinder(QTextBoundaryFinder::Grapheme, text);
    while (boundaryFinder.toNextBoundary() != -1) {
        ++count;
    }
    return count;
}

int KStandardItemListWidget::selectionLength(const QString &text) const
{
    return numberOfUnicodeCharactersIn(text);
}

void KStandardItemListWidget::editedRoleChanged(const QByteArray &current, const QByteArray &previous)
{
    Q_UNUSED(previous)

    QGraphicsView *parent = !scene() || scene()->views().isEmpty() ? nullptr : scene()->views()[0];
    if (current.isEmpty() || !parent || current != "text") {
        if (m_roleEditor) {
            Q_EMIT roleEditingCanceled(index(), current, data().value(current));
            closeRoleEditor();
        }
        return;
    }

    Q_ASSERT(!m_roleEditor);

    m_roleEditor = new KItemListRoleEditor(parent);
    m_roleEditor->setRole(current);
    m_roleEditor->setAllowUpDownKeyChainEdit(m_layout != IconsLayout);
    m_roleEditor->setFont(styleOption().font);

    const QString text = data().value(current).toString();
    m_roleEditor->setPlainText(text);

    const TextInfo *textInfo = m_textInfo.value("text");
    if (textInfo) {
        QTextOption textOption = textInfo->staticText.textOption();
        m_roleEditor->document()->setDefaultTextOption(textOption);
    }

    const int textSelectionLength = selectionLength(text);

    if (textSelectionLength > 0) {
        QTextCursor cursor = m_roleEditor->textCursor();
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, textSelectionLength);
        m_roleEditor->setTextCursor(cursor);
    }

    connect(m_roleEditor, &KItemListRoleEditor::roleEditingCanceled, this, &KStandardItemListWidget::slotRoleEditingCanceled);
    connect(m_roleEditor, &KItemListRoleEditor::roleEditingFinished, this, &KStandardItemListWidget::slotRoleEditingFinished);

    updateRoleEditorGeometry();
    m_roleEditor->show();
    m_roleEditor->setFocus();
    setHovered(false);
    update();
}

void KStandardItemListWidget::iconSizeChanged(int current, int previous)
{
    KItemListWidget::iconSizeChanged(current, previous);

    invalidateIconCache();
    triggerCacheRefreshing();
    update();
}

void KStandardItemListWidget::resizeEvent(QGraphicsSceneResizeEvent *event)
{
    if (m_roleEditor) {
        setEditedRole(QByteArray());
        Q_ASSERT(!m_roleEditor);
    }

    KItemListWidget::resizeEvent(event);

    m_dirtyLayout = true;
}

void KStandardItemListWidget::showEvent(QShowEvent *event)
{
    KItemListWidget::showEvent(event);

    if (m_roleEditor) {
        m_roleEditor->setFinishedSignalBlocked(false);
        updateRoleEditorGeometry();
        m_roleEditor->show();
        m_roleEditor->setFocus();
    }

    // Listen to changes of the clipboard to mark the item as cut/uncut
    KFileItemClipboard *clipboard = KFileItemClipboard::instance();

    const QUrl itemUrl = data().value("url").toUrl();
    m_isCut = clipboard->isCut(itemUrl);

    connect(clipboard, &KFileItemClipboard::cutItemsChanged, this, &KStandardItemListWidget::slotCutItemsChanged);
}

void KStandardItemListWidget::hideEvent(QHideEvent *event)
{
    if (m_roleEditor) {
        m_roleEditor->setFinishedSignalBlocked(true);
        m_roleEditor->hide();
    }

    disconnect(KFileItemClipboard::instance(), &KFileItemClipboard::cutItemsChanged, this, &KStandardItemListWidget::slotCutItemsChanged);

    KItemListWidget::hideEvent(event);
}

bool KStandardItemListWidget::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowActivate || event->type() == QEvent::PaletteChange) {
        m_dirtyContent = true;
    }

    return KItemListWidget::event(event);
}

void KStandardItemListWidget::finishRoleEditing()
{
    if (!editedRole().isEmpty() && m_roleEditor) {
        slotRoleEditingFinished(editedRole(), KIO::encodeFileName(m_roleEditor->toPlainText()));
    }
}

void KStandardItemListWidget::cancelRoleEditing()
{
    if (!editedRole().isEmpty() && m_roleEditor) {
        slotRoleEditingCanceled(editedRole(), KIO::encodeFileName(m_roleEditor->toPlainText()));
    }
}

void KStandardItemListWidget::updateRoleEditorGeometry()
{
    if (!m_roleEditor || editedRole().isEmpty() || !scene() || scene()->views().isEmpty()) {
        return;
    }

    auto *parent = scene()->views()[0];
    QRectF rect = roleEditingRect(editedRole());
    const int frameWidth = m_roleEditor->frameWidth();
    rect.adjust(-frameWidth, -frameWidth, frameWidth, frameWidth);
    rect.translate(pos());
    if (rect.right() > parent->width()) {
        rect.setWidth(parent->width() - rect.left());
    }
    m_roleEditor->setGeometry(rect.toRect());
    m_roleEditor->autoAdjustSize();
}

void KStandardItemListWidget::slotCutItemsChanged()
{
    const QUrl itemUrl = data().value("url").toUrl();
    const bool isCut = KFileItemClipboard::instance()->isCut(itemUrl);
    if (m_isCut != isCut) {
        m_isCut = isCut;
        m_pixmap = QPixmap();
        m_dirtyContent = true;
        update();
    }
}

void KStandardItemListWidget::slotRoleEditingCanceled(const QByteArray &role, const QVariant &value)
{
    closeRoleEditor();
    Q_EMIT roleEditingCanceled(index(), role, value);
    setEditedRole(QByteArray());
}

void KStandardItemListWidget::slotRoleEditingFinished(const QByteArray &role, const QVariant &value)
{
    closeRoleEditor();
    Q_EMIT roleEditingFinished(index(), role, value);
    setEditedRole(QByteArray());
}

void KStandardItemListWidget::triggerCacheRefreshing()
{
    if ((!m_dirtyContent && !m_dirtyLayout) || index() < 0) {
        return;
    }

    refreshCache();

    const QHash<QByteArray, QVariant> values = data();
    m_isExpandable = m_supportsItemExpanding && values["isExpandable"].toBool();
    m_isHidden = isHidden();
    m_customizedFont = customizedFont(styleOption().font);
    m_customizedFontMetrics = QFontMetrics(m_customizedFont);
    m_columnWidthSum = std::accumulate(m_sortedVisibleRoles.begin(), m_sortedVisibleRoles.end(), qreal(), [this](qreal sum, const auto &role) {
        return sum + columnWidth(role);
    });

    updateExpansionArea();
    updateTextsCache();
    updatePixmapCache();
    clearHoverCache();

    m_dirtyLayout = false;
    m_dirtyContent = false;
    m_dirtyContentRoles.clear();
}

void KStandardItemListWidget::updateExpansionArea()
{
    if (m_supportsItemExpanding) {
        const QHash<QByteArray, QVariant> values = data();
        const int expandedParentsCount = values.value("expandedParentsCount", 0).toInt();
        if (expandedParentsCount >= 0) {
            const int widgetIconSize = styleOption().iconSize;
            const qreal widgetHeight = size().height();
            const qreal inc = (widgetHeight - widgetIconSize) / 2;
            const qreal x = expandedParentsCount * widgetHeight + inc;
            const qreal y = inc;
            if (layoutDirection() == Qt::LeftToRight) {
                const qreal leftPaddingWidth = m_highlightEntireRow ? leftPadding() : 0;
                m_expansionArea = QRectF(leftPaddingWidth + x, y, widgetIconSize, widgetIconSize);
                return;
            }
            const qreal rightPaddingWidth = m_highlightEntireRow ? rightPadding() : 0;
            m_expansionArea = QRectF(size().width() - rightPaddingWidth - x - widgetIconSize, y, widgetIconSize, widgetIconSize);
            return;
        }
    }

    m_expansionArea = QRectF();
}

void KStandardItemListWidget::updatePixmapCache()
{
    // Precondition: Requires already updated m_textPos values to calculate
    // the remaining height when the alignment is vertical.

    const QSizeF widgetSize = size();
    const bool iconOnTop = (m_layout == IconsLayout);
    const KItemListStyleOption &option = styleOption();
    const qreal padding = option.padding;
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    const int widgetIconSize = option.iconSize;
    const int maxIconWidth = iconOnTop ? widgetSize.width() - 2 * padding : widgetIconSize;
    const int maxIconHeight = widgetIconSize;

    const QHash<QByteArray, QVariant> values = data();

    bool updatePixmap = (m_pixmap.width() != maxIconWidth || m_pixmap.height() != maxIconHeight);
    if (!updatePixmap && m_dirtyContent) {
        updatePixmap = m_dirtyContentRoles.isEmpty() || m_dirtyContentRoles.contains("iconPixmap") || m_dirtyContentRoles.contains("iconName")
            || m_dirtyContentRoles.contains("iconOverlays");
    }

    if (updatePixmap) {
        m_pixmap = QPixmap();

        int sequenceIndex = hoverSequenceIndex();

        if (values.contains("hoverSequencePixmaps") && !isIconControlledByActivateSoonAnimation()) {
            // Use one of the hover sequence pixmaps instead of the default
            // icon pixmap.

            const QVector<QPixmap> pixmaps = values["hoverSequencePixmaps"].value<QVector<QPixmap>>();

            if (values.contains("hoverSequenceWraparoundPoint")) {
                const float wap = values["hoverSequenceWraparoundPoint"].toFloat();
                if (wap >= 1.0f) {
                    sequenceIndex %= static_cast<int>(wap);
                }
            }

            const int loadedIndex = qMax(qMin(sequenceIndex, pixmaps.size() - 1), 0);

            if (loadedIndex != 0) {
                m_pixmap = pixmaps[loadedIndex];
            }
        }

        if (m_pixmap.isNull() && !isIconControlledByActivateSoonAnimation()) {
            m_pixmap = values["iconPixmap"].value<QPixmap>();
        }

        if (m_pixmap.isNull()) {
            // Use the icon that fits to the MIME-type
            QString iconName = values["iconName"].toString();
            if (iconName.isEmpty()) {
                // The icon-name has not been not resolved by KFileItemModelRolesUpdater,
                // use a generic icon as fallback
                iconName = QStringLiteral("unknown");
            }
            const bool hasFocus = scene()->views()[0]->parentWidget()->hasFocus();
            m_pixmap = pixmapForIcon(iconName,
                                     QSize(maxIconWidth, maxIconHeight),
                                     m_layout != IconsLayout && isActiveWindow() && isPressed() && hasFocus ? QIcon::Selected : QIcon::Normal);

        } else {
            if (m_pixmap.width() / m_pixmap.devicePixelRatio() != maxIconWidth || m_pixmap.height() / m_pixmap.devicePixelRatio() != maxIconHeight) {
                // A custom pixmap has been applied. Assure that the pixmap
                // is scaled to the maximum available size.
                KPixmapModifier::scale(m_pixmap, QSize(maxIconWidth, maxIconHeight) * dpr);
            }
        }

        if (m_pixmap.isNull()) {
            return;
        }

        if (m_isCut) {
            KIconEffect::toDisabled(m_pixmap);
        }

        if (m_isHidden) {
            KIconEffect::semiTransparent(m_pixmap);
        }
    }

    int scaledIconSize = 0;
    if (iconOnTop) {
        const TextInfo *textInfo = m_textInfo.value("text");
        scaledIconSize = static_cast<int>(textInfo->pos.y() - 2 * padding);
    } else {
        const int textRowsCount = (m_layout == CompactLayout) ? visibleRoles().count() : 1;
        const qreal requiredTextHeight = textRowsCount * m_customizedFontMetrics.height();
        scaledIconSize = (requiredTextHeight < maxIconHeight) ? widgetSize.height() - 2 * padding : maxIconHeight;
    }

    const int maxScaledIconWidth = iconOnTop ? widgetSize.width() - 2 * padding : scaledIconSize;
    const int maxScaledIconHeight = scaledIconSize;

    m_scaledPixmapSize = m_pixmap.size();
    m_scaledPixmapSize.scale(maxScaledIconWidth * dpr, maxScaledIconHeight * dpr, Qt::KeepAspectRatio);
    m_scaledPixmapSize = m_scaledPixmapSize / dpr;

    if (iconOnTop) {
        // Center horizontally and align on bottom within the icon-area
        m_pixmapPos.setX((widgetSize.width() - m_scaledPixmapSize.width()) / 2.0);
        m_pixmapPos.setY(padding + scaledIconSize - m_scaledPixmapSize.height());
    } else {
        // Center horizontally and vertically within the icon-area
        const TextInfo *textInfo = m_textInfo.value("text");
        if (QApplication::isRightToLeft()) {
            m_pixmapPos.setX(m_textRect.right() + 2.0 * padding);
        } else {
            m_pixmapPos.setX(textInfo->pos.x() - 2.0 * padding - (scaledIconSize + m_scaledPixmapSize.width()) / 2.0);
        }

        // Derive icon's vertical center from the center of the text frame, including
        // any necessary adjustment if the font's midline is offset from the frame center
        const qreal midlineShift = m_customizedFontMetrics.height() / 2.0 - m_customizedFontMetrics.descent() - m_customizedFontMetrics.capHeight() / 2.0;
        m_pixmapPos.setY(m_textRect.center().y() + midlineShift - m_scaledPixmapSize.height() / 2.0);
    }

    if (m_layout == IconsLayout) {
        m_iconRect = QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize));
    } else {
        const qreal widthOffset = widgetIconSize - m_scaledPixmapSize.width();
        const qreal heightOffset = widgetIconSize - m_scaledPixmapSize.height();
        const QPointF squareIconPos(m_pixmapPos.x() - 0.5 * widthOffset, m_pixmapPos.y() - 0.5 * heightOffset);
        const QSizeF squareIconSize(widgetIconSize, widgetIconSize);
        m_iconRect = QRectF(squareIconPos, squareIconSize);
    }
}

void KStandardItemListWidget::updateTextsCache()
{
    QTextOption textOption;
    switch (m_layout) {
    case IconsLayout:
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textOption.setAlignment(Qt::AlignHCenter);
        break;
    case CompactLayout:
        textOption.setAlignment(QApplication::isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft);
        textOption.setWrapMode(QTextOption::NoWrap);
        break;
    case DetailsLayout:
        textOption.setAlignment(Qt::AlignLeft);
        textOption.setWrapMode(QTextOption::NoWrap);
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    qDeleteAll(m_textInfo);
    m_textInfo.clear();
    for (int i = 0; i < m_sortedVisibleRoles.count(); ++i) {
        TextInfo *textInfo = new TextInfo();
        textInfo->staticText.setTextFormat(Qt::PlainText);
        textInfo->staticText.setPerformanceHint(QStaticText::AggressiveCaching);
        textInfo->staticText.setTextOption(textOption);
        m_textInfo.insert(m_sortedVisibleRoles[i], textInfo);
    }

    switch (m_layout) {
    case IconsLayout:
        updateIconsLayoutTextCache();
        break;
    case CompactLayout:
        updateCompactLayoutTextCache();
        break;
    case DetailsLayout:
        updateDetailsLayoutTextCache();
        break;
    default:
        Q_ASSERT(false);
        break;
    }

    const TextInfo *ratingTextInfo = m_textInfo.value("rating");
    if (ratingTextInfo) {
        // The text of the rating-role has been set to empty to get
        // replaced by a rating-image showing the rating as stars.
        const KItemListStyleOption &option = styleOption();
        QSizeF ratingSize = preferredRatingSize(option);

        const qreal availableWidth = (m_layout == DetailsLayout) ? columnWidth("rating") - columnPadding(option) : size().width();
        if (ratingSize.width() > availableWidth) {
            ratingSize.rwidth() = availableWidth;
        }
        const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
        m_rating = QPixmap(ratingSize.toSize() * dpr);
        m_rating.setDevicePixelRatio(dpr);
        m_rating.fill(Qt::transparent);

        QPainter painter(&m_rating);
        const QRect rect(QPoint(0, 0), ratingSize.toSize());
        const int rating = data().value("rating").toInt();
        KRatingPainter::paintRating(&painter, rect, Qt::AlignJustify | Qt::AlignVCenter, rating);
    } else if (!m_rating.isNull()) {
        m_rating = QPixmap();
    }
}

QString KStandardItemListWidget::elideText(QString text, qreal maxWidth) const
{
    if (ContentDisplaySettings::elidingMode() == ContentDisplaySettings::ElidingMode::Middle) {
        return m_customizedFontMetrics.elidedText(text, Qt::ElideMiddle, maxWidth);
    }

    if (ContentDisplaySettings::elidingMode() == ContentDisplaySettings::ElidingMode::Right) {
        qsizetype lastDotPosition = text.lastIndexOf(".");
        QString extension = text.mid(lastDotPosition);

        if (m_customizedFontMetrics.horizontalAdvance(QStringLiteral("…") + extension) > maxWidth) {
            extension = "";
            lastDotPosition = text.size();
        }

        maxWidth -= m_customizedFontMetrics.horizontalAdvance(extension);
        QString leftPart = m_customizedFontMetrics.elidedText(text.left(lastDotPosition), Qt::ElideRight, maxWidth);

        return leftPart + extension;
    }

    Q_UNREACHABLE();
    return text;
}

QString KStandardItemListWidget::escapeString(const QString &text) const
{
    QString escaped(text);

    const QChar returnSymbol(0x21b5);
    escaped.replace('\n', returnSymbol);

    return escaped;
}

void KStandardItemListWidget::updateIconsLayoutTextCache()
{
    //      +------+
    //      | Icon |
    //      +------+
    //
    //    Name role that
    // might get wrapped above
    //    several lines.
    //  Additional role 1
    //  Additional role 2

    const QHash<QByteArray, QVariant> values = data();

    const KItemListStyleOption &option = styleOption();
    const qreal padding = option.padding;
    const qreal maxWidth = size().width() - 2 * padding;
    const qreal lineSpacing = m_customizedFontMetrics.lineSpacing();

    // Initialize properties for the "text" role. It will be used as anchor
    // for initializing the position of the other roles.
    TextInfo *nameTextInfo = m_textInfo.value("text");
    const QString nameText = KStringHandler::preProcessWrap(escapeString(values["text"].toString()));
    nameTextInfo->staticText.setText(nameText);

    // Calculate the number of lines required for the name and the required width
    qreal nameWidth = 0;
    qreal nameHeight = 0;
    QTextLine line;

    QTextLayout layout(nameTextInfo->staticText.text(), m_customizedFont);
    layout.setTextOption(nameTextInfo->staticText.textOption());
    layout.beginLayout();
    int nameLineIndex = 0;
    while ((line = layout.createLine()).isValid()) {
        line.setLineWidth(maxWidth);
        nameWidth = qMax(nameWidth, line.naturalTextWidth());
        nameHeight += line.height();

        ++nameLineIndex;
        if (nameLineIndex == option.maxTextLines) {
            // The maximum number of textlines has been reached. If this is
            // the case provide an elided text if necessary.
            const int textLength = line.textStart() + line.textLength();
            if (textLength < nameText.length()) {
                // Elide the last line of the text
                qreal elidingWidth = maxWidth;
                qreal lastLineWidth;
                do {
                    QString lastTextLine = nameText.mid(line.textStart());
                    lastTextLine = elideText(lastTextLine, elidingWidth);
                    const QString elidedText = nameText.left(line.textStart()) + lastTextLine;
                    nameTextInfo->staticText.setText(elidedText);

                    lastLineWidth = m_customizedFontMetrics.size(0, lastTextLine).width();

                    // We do the text eliding in a loop with decreasing width (1 px / iteration)
                    // to avoid problems related to different width calculation code paths
                    // within Qt. (see bug 337104)
                    elidingWidth -= 1.0;
                } while (lastLineWidth > maxWidth);

                nameWidth = qMax(nameWidth, lastLineWidth);
            }
            break;
        }
    }
    layout.endLayout();

    // Use one line for each additional information
    nameTextInfo->staticText.setTextWidth(maxWidth);
    nameTextInfo->pos = QPointF(padding, styleOption().iconSize + 2 * padding);
    m_textRect = QRectF(padding + (maxWidth - nameWidth) / 2, nameTextInfo->pos.y(), nameWidth, nameHeight);

    // Calculate the position for each additional information
    qreal y = nameTextInfo->pos.y() + nameHeight;
    for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
        if (role == "text") {
            continue;
        }

        const QString text = roleText(role, values);
        TextInfo *textInfo = m_textInfo.value(role);
        textInfo->staticText.setText(text);

        qreal requiredWidth = 0;

        QTextLayout layout(text, m_customizedFont);
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(textOption);

        layout.beginLayout();
        QTextLine textLine = layout.createLine();
        if (textLine.isValid()) {
            textLine.setLineWidth(maxWidth);
            requiredWidth = textLine.naturalTextWidth();
            if (requiredWidth > maxWidth) {
                const QString elidedText = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
                textInfo->staticText.setText(elidedText);
                requiredWidth = m_customizedFontMetrics.horizontalAdvance(elidedText);
            } else if (role == "rating") {
                // Use the width of the rating pixmap, because the rating text is empty.
                requiredWidth = m_rating.width() / m_rating.devicePixelRatioF();
            }
        }
        layout.endLayout();

        textInfo->pos = QPointF(padding, y);
        textInfo->staticText.setTextWidth(maxWidth);

        const QRectF textRect(padding + (maxWidth - requiredWidth) / 2, y, requiredWidth, lineSpacing);

        // Ignore empty roles. Avoids a text rect taller than the area that actually contains text.
        if (!textRect.isEmpty()) {
            m_textRect |= textRect;
        }

        y += lineSpacing;
    }

    // Add a padding to the text rectangle
    m_textRect.adjust(-padding, -padding, padding, padding);
}

void KStandardItemListWidget::updateCompactLayoutTextCache()
{
    // +------+  Name role
    // | Icon |  Additional role 1
    // +------+  Additional role 2

    const QHash<QByteArray, QVariant> values = data();

    const KItemListStyleOption &option = styleOption();
    const qreal widgetHeight = size().height();
    const qreal lineSpacing = m_customizedFontMetrics.lineSpacing();
    const qreal textLinesHeight = qMax(visibleRoles().count(), 1) * lineSpacing;

    qreal maximumRequiredTextWidth = 0;
    const qreal x = QApplication::isRightToLeft() ? option.padding : option.padding * 3 + styleOption().iconSize;
    qreal y = qRound((widgetHeight - textLinesHeight) / 2);
    const qreal maxWidth = size().width() - styleOption().iconSize - 4 * option.padding;
    for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
        const QString text = escapeString(roleText(role, values));
        TextInfo *textInfo = m_textInfo.value(role);
        textInfo->staticText.setText(text);

        qreal requiredWidth = m_customizedFontMetrics.horizontalAdvance(text);
        if (requiredWidth > maxWidth) {
            requiredWidth = maxWidth;
            if (role == "text") {
                const QString elidedText = elideText(text, maxWidth);
                textInfo->staticText.setText(elidedText);
            } else {
                const QString elidedText = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, maxWidth);
                textInfo->staticText.setText(elidedText);
            }
        }

        textInfo->pos = QPointF(x, y);
        textInfo->staticText.setTextWidth(maxWidth);

        maximumRequiredTextWidth = qMax(maximumRequiredTextWidth, requiredWidth);

        y += lineSpacing;
    }

    m_textRect = QRectF(x - option.padding, 0, maximumRequiredTextWidth + 2 * option.padding, widgetHeight);
}

void KStandardItemListWidget::updateDetailsLayoutTextCache()
{
    // Precondition: Requires already updated m_expansionArea
    // to determine the left position.

    // +------+
    // | Icon |  Name role   Additional role 1   Additional role 2
    // +------+
    // Mirror the above for right-to-left languages.
    const bool isLeftToRight = QApplication::layoutDirection() == Qt::LeftToRight;
    m_textRect = QRectF();

    const KItemListStyleOption &option = styleOption();
    const QHash<QByteArray, QVariant> values = data();

    const qreal widgetHeight = size().height();
    const int fontHeight = m_customizedFontMetrics.height();

    const qreal columnWidthInc = columnPadding(option);
    qreal firstColumnInc = styleOption().iconSize;
    if (m_supportsItemExpanding) {
        firstColumnInc += isLeftToRight ? (m_expansionArea.left() + m_expansionArea.right() + widgetHeight) / 2
                                        : ((size().width() - m_expansionArea.left()) + (size().width() - m_expansionArea.right()) + widgetHeight) / 2;
    } else {
        firstColumnInc += option.padding + (isLeftToRight ? leftPadding() : rightPadding());
    }

    qreal x = firstColumnInc;
    const qreal standardY = qMax(qreal(option.padding), (widgetHeight - fontHeight) / 2.0);

    if (neoTwoLineList()) {
        // dolphin-neo, with icons:   +------+  Name                                            Date
        //                            | Icon |  Size · Type · other roles
        // dolphin-neo, icon-less:    TYPE (watermark)  Name                        Size        Date / actions on hover
        //                                              Type · other roles          [bar]
        static const QSet<QByteArray> dateRoles{"modificationtime", "creationtime", "accesstime", "deletiontime"};
        const bool noIcons = neoNoIcons();
        QByteArray dateRole;
        for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
            if (dateRoles.contains(role)) {
                dateRole = role;
                break;
            }
        }
        m_neoDateRole = dateRole;
        const qreal gap = columnWidthInc / 2;
        const qreal left = noIcons ? leftPadding() + 14 + widgetHeight * 0.62 + 8 + fontHeight * 5.4 : firstColumnInc + gap;
        const qreal right = size().width() - 18;
        const int lineGap = 2;
        const qreal firstY = qMax(qreal(option.padding), (widgetHeight - (2 * fontHeight + lineGap)) / 2.0);
        const qreal secondY = firstY + fontHeight + lineGap;

        // a narrow view drops the date first, then the size column
        const bool roomForDate = !noIcons || size().width() >= fontHeight * 36;
        const bool roomForSize = size().width() >= fontHeight * 26;
        if (!dateRole.isEmpty() && !roomForDate) {
            m_textInfo.value(dateRole)->staticText.setText(QString());
        }

        // right edge: the date (replaced by the actions while the row is hovered)
        qreal dateWidth = 0;
        if (!dateRole.isEmpty() && roomForDate) {
            const QString text = roleText(dateRole, values);
            dateWidth = m_customizedFontMetrics.horizontalAdvance(text);
            TextInfo *textInfo = m_textInfo.value(dateRole);
            textInfo->staticText.setText(text);
            textInfo->pos = QPointF(right - dateWidth, standardY);
        }
        qreal blockLeft = right;
        m_neoActionsRect = QRectF();
        if (noIcons) {
            const qreal dateColumn = roomForDate ? qMax(qreal(fontHeight * 9), qreal(176)) : qreal(fontHeight * 8.2); // fixed, so that the sizes line up
            const qreal actionsWidth = qMin(dateColumn, qreal(fontHeight * 8.2));
            const qreal actionsHeight = qMin(widgetHeight - 12, qreal(fontHeight * 1.7));
            m_neoActionsRect = QRectF(right - actionsWidth, (widgetHeight - actionsHeight) / 2.0, actionsWidth, actionsHeight);
            blockLeft = right - dateColumn;
        } else if (dateWidth > 0) {
            blockLeft = right - dateWidth - 3 * gap;
        }
        if (m_neoActions) {
            m_neoActions->setGeometry(m_neoActionsRect);
        }

        // icon-less: the size sits in its own column, with a bar on a logarithmic scale (1 KiB ... 10 GiB)
        m_neoBarFraction = -1;
        m_neoBarRect = QRectF();
        const bool sizeColumn = noIcons && m_sortedVisibleRoles.contains("size");
        if (sizeColumn && !roomForSize) {
            m_textInfo.value("size")->staticText.setText(QString());
        }
        if (sizeColumn && roomForSize) {
            const qreal columnWidth = qMax(qreal(fontHeight * 6.5), qreal(130));
            const qreal sizeRight = blockLeft - gap;
            const QString text = roleText("size", values);
            const qreal width = m_customizedFontMetrics.horizontalAdvance(text);
            TextInfo *textInfo = m_textInfo.value("size");
            textInfo->staticText.setText(text);
            textInfo->pos = QPointF(sizeRight - width, firstY);
            const qlonglong bytes = values.value("size").toLongLong();
            if (!values.value("isDir").toBool() && bytes > 0) {
                m_neoBarFraction = qBound(0.03, (std::log10(double(bytes)) - 3.0) / 7.0, 1.0);
                const qreal barWidth = columnWidth - 10;
                m_neoBarRect = QRectF(sizeRight - barWidth, secondY + fontHeight / 2.0 - 2, barWidth, 4);
            }
            blockLeft = sizeRight - columnWidth;
        }
        const qreal textRight = blockLeft - 2 * gap;

        if (noIcons) {
            const NeoKind kind = neoKindFor(values.value("text").toString(), values.value("isDir").toBool());
            m_neoSigla = kind.sigla;
            m_neoTint = kind.tint;
            const QString iconName = values.value("iconName").toString();
            const int iconSide = qMax(16, qRound(widgetHeight * 0.62));
            if (iconName != m_neoIconName || m_neoIcon.isNull() || qRound(m_neoIcon.width() / m_neoIcon.devicePixelRatio()) != iconSide) {
                m_neoIconName = iconName;
                m_neoIcon = iconName.isEmpty() ? QPixmap() : QIcon::fromTheme(iconName).pixmap(QSize(iconSide, iconSide), KItemViewsUtils::devicePixelRatio(this));
            }
            // the row, the abbreviation and the size bar take the main colour of the type icon
            const QColor iconColor = neoIconColor(iconName, m_neoIcon);
            if (iconColor.isValid()) {
                m_neoTint = iconColor;
            }
        } else {
            m_neoSigla.clear();
        }

        qreal secondX = left;
        for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
            TextInfo *textInfo = m_textInfo.value(role);
            if (role == "text") {
                QString text = escapeString(roleText(role, values));
                qreal width = m_customizedFontMetrics.horizontalAdvance(text);
                const qreal available = qMax(qreal(0), textRight - left);
                if (width > available) {
                    text = elideText(text, available);
                    width = m_customizedFontMetrics.horizontalAdvance(text);
                }
                textInfo->staticText.setText(text);
                textInfo->pos = QPointF(left, firstY);
                m_textRect = QRectF(left - option.padding, 0, width + 2 * option.padding, widgetHeight);
            } else if (role != dateRole && !(sizeColumn && role == "size")) {
                QString text = roleText(role, values);
                if (!text.isEmpty() && secondX > left) {
                    text.prepend(QStringLiteral("\u00B7  "));
                }
                qreal width = m_customizedFontMetrics.horizontalAdvance(text);
                const qreal available = textRight - secondX;
                if (width > available) {
                    text = available > 3 * fontHeight ? m_customizedFontMetrics.elidedText(text, Qt::ElideRight, available) : QString();
                    width = m_customizedFontMetrics.horizontalAdvance(text);
                }
                textInfo->staticText.setText(text);
                textInfo->pos = QPointF(secondX, secondY);
                if (!text.isEmpty()) {
                    secondX += width + gap;
                }
            }
        }
        return;
    }

    for (const QByteArray &role : std::as_const(m_sortedVisibleRoles)) {
        QString text = roleText(role, values);

        // Elide the text in case it does not fit into the available column-width
        qreal requiredWidth = m_customizedFontMetrics.horizontalAdvance(text);
        const qreal roleWidth = columnWidth(role);
        qreal availableTextWidth = roleWidth - columnWidthInc;

        const bool isTextRole = (role == "text");
        if (isTextRole) {
            text = escapeString(text);
            availableTextWidth -= firstColumnInc - (isLeftToRight ? leftPadding() : rightPadding());
        }

        if (requiredWidth > availableTextWidth) {
            if (isTextRole) {
                text = elideText(text, availableTextWidth);
            } else {
                text = m_customizedFontMetrics.elidedText(text, Qt::ElideRight, availableTextWidth);
            }
            requiredWidth = m_customizedFontMetrics.horizontalAdvance(text);
        }

        TextInfo *textInfo = m_textInfo.value(role);
        textInfo->staticText.setText(text);
        qreal y = standardY;
        if (isTextRole) {
            QTextLayout layout(text, m_customizedFont);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            layout.endLayout();
            if (line.isValid()) {
                y = standardY + (m_customizedFontMetrics.ascent() - line.ascent());
            }
        }
        textInfo->pos = QPointF(isLeftToRight ? (x + columnWidthInc / 2) : (size().width() - (x + columnWidthInc / 2) - requiredWidth), y);
        x += roleWidth;

        if (isTextRole) {
            m_textRect = QRectF(textInfo->pos.x() - option.padding, 0, requiredWidth + 2 * option.padding, size().height());

            // The column after the name should always be aligned on the same x-position independent
            // from the expansion-level shown in the name column
            x -= firstColumnInc - (isLeftToRight ? leftPadding() : rightPadding());
        } else if (isRoleRightAligned(role) && isLeftToRight) {
            textInfo->pos.rx() += roleWidth - requiredWidth - columnWidthInc;
        }
    }
}

void KStandardItemListWidget::updateAdditionalInfoTextColor()
{
    QColor c1;
    const bool hasFocus = scene()->views()[0]->parentWidget()->hasFocus();
    if (m_customTextColor.isValid()) {
        c1 = m_customTextColor;
    } else if (isSelected() && hasFocus && (m_layout != DetailsLayout || m_highlightEntireRow)) {
        // The detail text color needs to match the main text (HighlightedText) for the same level
        // of readability. We short circuit early here to avoid interpolating with another color.
        m_additionalInfoTextColor = styleOption().palette.color(normalTextColorRole());
        return;
    } else {
        c1 = styleOption().palette.text().color();
    }

    // For the color of the additional info the inactive text color
    // is not used as this might lead to unreadable text for some color schemes. Instead
    // the text color c1 is slightly mixed with the background color.
    const QColor c2 = styleOption().palette.base().color();
    const int p1 = 70;
    const int p2 = 100 - p1;
    m_additionalInfoTextColor =
        QColor((c1.red() * p1 + c2.red() * p2) / 100, (c1.green() * p1 + c2.green() * p2) / 100, (c1.blue() * p1 + c2.blue() * p2) / 100);
}

void KStandardItemListWidget::drawOverlays(QPainter *painter) const
{
    // similar to KIconUtils::addOverlays, keep in sync preferably
    if (m_overlays.isEmpty()) {
        return;
    }

    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    // Size depends only on the zoom-level icon size, so it is consistent between
    // files regardless of thumbnail aspect ratio.
    const int iconSize = styleOption().iconSize;
    int overlaySize;
    if (iconSize < 32) {
        overlaySize = 8;
    } else if (iconSize <= 48) {
        overlaySize = 16;
    } else if (iconSize <= 96) {
        overlaySize = 22;
    } else if (iconSize < 256) {
        overlaySize = 32;
    } else {
        overlaySize = 64;
    }

    // Anchor to the corners of the icon as actually drawn on screen.
    QRectF iconRect(m_pixmapPos, QSizeF(m_scaledPixmapSize));

    // Clamp to the thumbnail height so the overlay never spills onto the file name
    // below a short thumbnail.
    overlaySize = qMin<int>(overlaySize, qFloor(iconRect.height()));
    if (overlaySize <= 0) {
        return;
    }

    // For a thumbnail narrower than the overlay, grow the anchor into the side
    // margins so the overlays sit beside it rather than on top of it.
    if (iconRect.width() < overlaySize) {
        iconRect.adjust(-overlaySize, 0, overlaySize, 0);
    }

    for (const auto &[corner, overlay] : m_overlays.asKeyValueRange()) {
        const QPixmap overlayPixmap = QIcon::fromTheme(overlay).pixmap(QSize{overlaySize, overlaySize}, dpr);
        if (overlayPixmap.isNull()) {
            continue;
        }

        QPointF startPoint;
        switch (corner) {
        case Qt::BottomLeftCorner:
            startPoint = QPointF{iconRect.left(), iconRect.bottom() - overlaySize};
            break;
        case Qt::BottomRightCorner:
            startPoint = QPointF{iconRect.right() - overlaySize, iconRect.bottom() - overlaySize};
            break;
        case Qt::TopRightCorner:
            startPoint = QPointF{iconRect.right() - overlaySize, iconRect.top()};
            break;
        case Qt::TopLeftCorner:
            startPoint = iconRect.topLeft();
            break;
        }
        painter->drawPixmap(startPoint, overlayPixmap);
    }
}

void KStandardItemListWidget::drawPixmap(QPainter *painter, const QPixmap &pixmap)
{
    // dolphin-neo: in the tiles of the icons view the picture gets rounded corners
    const bool rounded = m_layout == IconsLayout && neoTiles();
    if (rounded) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize)), 9, 9);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(clip, Qt::IntersectClip);
    }
    auto done = qScopeGuard([painter, rounded] {
        if (rounded) {
            painter->restore();
        }
    });

    if (m_scaledPixmapSize != pixmap.size() / pixmap.devicePixelRatio()) {
        const qreal dpr = KItemViewsUtils::devicePixelRatio(this);
        QPixmap scaledPixmap = pixmap;
        KPixmapModifier::scale(scaledPixmap, m_scaledPixmapSize * dpr);
        scaledPixmap.setDevicePixelRatio(dpr);
        painter->drawPixmap(m_pixmapPos, scaledPixmap);

#ifdef KSTANDARDITEMLISTWIDGET_DEBUG
        painter->setPen(Qt::blue);
        painter->drawRect(QRectF(m_pixmapPos, QSizeF(m_scaledPixmapSize)));
#endif
    } else {
        painter->drawPixmap(m_pixmapPos, pixmap);
    }
}

void KStandardItemListWidget::drawSiblingsInformation(QPainter *painter)
{
    const int siblingSize = size().height();
    const int x = (m_expansionArea.left() + m_expansionArea.right() - siblingSize) / 2;
    QRect siblingRect(x, 0, siblingSize, siblingSize);

    bool isItemSibling = true;

    const QBitArray siblings = siblingsInformation();
    QStyleOption option;
    for (int i = siblings.count() - 1; i >= 0; --i) {
        option.rect = siblingRect;
        option.state = siblings.at(i) ? QStyle::State_Sibling : QStyle::State_None;
        if (isItemSibling) {
            option.state |= QStyle::State_Item;
            if (m_isExpandable) {
                option.state |= QStyle::State_Children;
            }
            if (data().value("isExpanded").toBool()) {
                option.state |= QStyle::State_Open;
            }
            isItemSibling = false;
        }

        style()->drawPrimitive(QStyle::PE_IndicatorBranch, &option, painter);

        siblingRect.translate(layoutDirection() == Qt::LeftToRight ? -siblingRect.width() : siblingRect.width(), 0);
    }
}

QRectF KStandardItemListWidget::roleEditingRect(const QByteArray &role) const
{
    const TextInfo *textInfo = m_textInfo.value(role);
    if (!textInfo) {
        return QRectF();
    }

    QRectF rect(textInfo->pos, textInfo->staticText.size());
    if (m_layout == DetailsLayout) {
        rect.setWidth(columnWidth(role) - rect.x());
    }

    return rect;
}

void KStandardItemListWidget::closeRoleEditor()
{
    disconnect(m_roleEditor, &KItemListRoleEditor::roleEditingCanceled, this, &KStandardItemListWidget::slotRoleEditingCanceled);
    disconnect(m_roleEditor, &KItemListRoleEditor::roleEditingFinished, this, &KStandardItemListWidget::slotRoleEditingFinished);

    if (m_roleEditor->hasFocus()) {
        // If the editing was not ended by a FocusOut event, we have
        // to transfer the keyboard focus back to the KItemListContainer.
        scene()->views()[0]->parentWidget()->setFocus();
    }

    if (m_oldRoleEditor) {
        m_oldRoleEditor->deleteLater();
    }
    m_oldRoleEditor = m_roleEditor;
    m_roleEditor->hide();
    m_roleEditor = nullptr;
}

QPixmap KStandardItemListWidget::pixmapForIcon(const QString &name, const QSize &size, QIcon::Mode mode) const
{
    static const QIcon fallbackIcon = QIcon::fromTheme(QStringLiteral("unknown"));
    const qreal dpr = KItemViewsUtils::devicePixelRatio(this);

    int iconHeight = size.height();
    QSize iconSize = QSize(iconHeight, iconHeight);

    const QString key = "KStandardItemListWidget:" % name % ":" % QString::number(iconHeight) % "@" % QString::number(dpr) % ":" % QString::number(mode);
    QPixmap pixmap;

    if (!QPixmapCache::find(key, &pixmap)) {
        QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull()) {
            icon = QIcon(name);
        }
        if (!icon.isNull()) {
            pixmap = icon.pixmap(iconSize, dpr, mode);
        }
        if (pixmap.isNull()) {
            icon = fallbackIcon;
            pixmap = icon.pixmap(iconSize, dpr, mode);
        }
        if (pixmap.width() != iconHeight * dpr || pixmap.height() != iconHeight * dpr) {
            KPixmapModifier::scale(pixmap, iconSize * dpr);
        }

        QPixmapCache::insert(key, pixmap);
    }
    pixmap.setDevicePixelRatio(dpr);

    return pixmap;
}

QSizeF KStandardItemListWidget::preferredRatingSize(const KItemListStyleOption &option)
{
    const qreal height = option.fontMetrics.ascent();
    return QSizeF(height * 5, height);
}

qreal KStandardItemListWidget::columnPadding(const KItemListStyleOption &option)
{
    return option.padding * 6;
}

#include "moc_kstandarditemlistwidget.cpp"

/*
 * SPDX-FileCopyrightText: 2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphindockwidget.h"

#include "kitemviews/kitemlistwidget.h"

#include <KConfig>
#include <KConfigGroup>
#include <KIO/Global>
#include <KLocalizedString>
#include <KWindowInfo>
#include <KWindowSystem>
#include <KX11Extras>

#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>

namespace
{
// dolphin-neo glass settings. DOLPHIN_NEO_GLASS=<0-100> fixes the transparency; without it the
// transparency follows the backdrop: DOLPHIN_NEO_GLASS_DARK (default 90) over black,
// DOLPHIN_NEO_GLASS_LIGHT (default 10) over white, linear in between.
int neoEnvInt(const char *name, int fallback)
{
    return qEnvironmentVariableIsSet(name) ? qBound(0, qEnvironmentVariableIntValue(name), 100) : fallback;
}
bool neoGlassPlain()
{
    static const bool plain = neoSetting("SIDEBAR").toLower() == QLatin1String("plain");
    return plain;
}

// DOLPHIN_NEO_GLASS_MODE=image (default): the card shows a static, blurred copy of the wallpaper, placed by the
// window position on screen. DOLPHIN_NEO_GLASS_MODE=live: the card is a hole showing the compositor's real blur.
bool neoGlassImageMode()
{
    static const bool live = neoSetting("GLASS_MODE").toLower() == QLatin1String("live");
    return !live;
}

// The image behind the glass: DOLPHIN_NEO_GLASS_IMAGE, else the Plasma wallpaper of that screen.
QString neoBackdropPath(const QScreen *screen)
{
    const QString custom = neoSetting("GLASS_IMAGE");
    if (!custom.isEmpty()) {
        return custom;
    }
    // Plasma numbers its screens with the primary one first
    int screenId = 0;
    if (screen != QGuiApplication::primaryScreen()) {
        screenId = 1;
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (const QScreen *other : screens) {
            if (other == screen) {
                break;
            }
            if (other != QGuiApplication::primaryScreen()) {
                ++screenId;
            }
        }
    }
    KConfig config(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"), KConfig::SimpleConfig);
    const KConfigGroup containments(&config, QStringLiteral("Containments"));
    QString fallback;
    const QStringList names = containments.groupList();
    for (const QString &name : names) {
        const KConfigGroup containment = containments.group(name);
        // the image is stored under the name of the wallpaper plugin in use (org.kde.image, or a third-party one)
        const QString plugin = containment.readEntry("wallpaperplugin", QStringLiteral("org.kde.image"));
        QString image = containment.group(QStringLiteral("Wallpaper")).group(plugin).group(QStringLiteral("General")).readEntry("Image");
        if (image.isEmpty()) {
            continue;
        }
        if (image.startsWith(QLatin1String("file://"))) {
            image = QUrl(image).toLocalFile();
        }
        if (containment.readEntry("lastScreen", -1) == screenId) {
            return image;
        }
        if (fallback.isEmpty()) {
            fallback = image;
        }
    }
    return fallback;
}

// A blurred copy of the backdrop for `screen`, at a quarter of the screen resolution (it is drawn scaled up).
// Null if there is no usable image.
QPixmap neoBackdrop(const QScreen *screen)
{
    static QHash<QString, QPixmap> cache;
    const QSize screenSize = screen->geometry().size();
    const int blur = qBound(0, neoSettingInt("GLASS_BLUR", 45), 100); // how soft the backdrop is
    const QString path = neoBackdropPath(screen);
    const QString key = screen->name() + QLatin1Char(':') + QString::number(screenSize.width()) + QLatin1Char('x') + QString::number(screenSize.height())
        + QLatin1Char(':') + QString::number(blur) + QLatin1Char(':') + path;
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd()) {
        return *cached;
    }

    QPixmap result;
    if (!path.isEmpty()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // the blur comes from resampling through a very small image: the stronger the blur, the smaller that image
        const QSize tiny = (screenSize / (12 + blur * 2)).expandedTo(QSize(4, 4));
        const QSize original = reader.size();
        if (original.isValid() && !tiny.isEmpty()) {
            reader.setScaledSize(original.scaled(tiny, Qt::KeepAspectRatioByExpanding)); // fill the screen, crop the excess
        }
        QImage image = reader.read();
        if (!image.isNull() && !tiny.isEmpty()) {
            QRect crop(QPoint(0, 0), tiny);
            crop.moveCenter(image.rect().center());
            image = image.copy(crop.intersected(image.rect()));
            image = image.scaled(tiny * 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            image = image.scaled(screenSize / 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            result = QPixmap::fromImage(image);
        }
    }
    cache.insert(key, result);
    return result;
}
}
#include <QStyle>

namespace
{
// Disable the 'Floatable' feature, i.e., the possibility to drag the
// dock widget out of the main window. This works around problems like
// https://bugs.kde.org/show_bug.cgi?id=288629
// https://bugs.kde.org/show_bug.cgi?id=322299
const QDockWidget::DockWidgetFeatures DefaultDockWidgetFeatures = QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable;
}

// Empty titlebar for the dock widgets when "Lock Layout" has been activated.
class DolphinDockTitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit DolphinDockTitleBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }
    ~DolphinDockTitleBar() override = default;

    QSize minimumSizeHint() const override
    {
        return QSize(0, 0);
    }

    QSize sizeHint() const override
    {
        return minimumSizeHint();
    }
};

DolphinDockWidget::DolphinDockWidget(const QString &title, QWidget *parent, Qt::WindowFlags flags)
    : QDockWidget(title, parent, flags)
    , m_locked(false)
    , m_dockTitleBar(nullptr)
{
    setFeatures(DefaultDockWidgetFeatures);

    // dolphin-neo: follow the backdrop unless a fixed transparency was requested
    if (!neoGlassPlain()) {
        startTimer(30000); // disk usage box
    }
    if (neoGlassImageMode()) {
        m_glass = neoEnvInt("DOLPHIN_NEO_GLASS", 55); // how much of the image shows through the window tint
    } else if (qEnvironmentVariableIsSet("DOLPHIN_NEO_GLASS")) {
        m_glass = neoEnvInt("DOLPHIN_NEO_GLASS", 75);
    } else if (!neoGlassPlain() && KWindowSystem::isPlatformX11()) {
        m_glassTimer = new QTimer(this);
        // DOLPHIN_NEO_GLASS_MS: polling interval of the backdrop, milliseconds (default 150)
        m_glassTimer->setInterval(qEnvironmentVariableIsSet("DOLPHIN_NEO_GLASS_MS") ? qMax(50, qEnvironmentVariableIntValue("DOLPHIN_NEO_GLASS_MS")) : 150);
        connect(m_glassTimer, &QTimer::timeout, this, &DolphinDockWidget::updateGlass);
        m_glassTimer->start();
    }
}

qreal DolphinDockWidget::backdropLuminance() const
{
    const QWidget *top = window();
    QScreen *screen = top ? top->screen() : nullptr;
    if (!screen) {
        return -1;
    }
    const WId self = top->winId();
    const QList<WId> stack = KX11Extras::stackingOrder(); // bottom to top
    const int selfIndex = stack.indexOf(self);
    if (selfIndex <= 0) {
        return -1;
    }

    // the glass card in native screen pixels
    const qreal dpr = devicePixelRatioF();
    const QRect card = QRect(mapToGlobal(QPoint(6, 6)) * dpr, (size() - QSize(12, 12)) * dpr);
    if (card.width() < 24 || card.height() < 24) {
        return -1;
    }

    // windows below ours that are visible and overlap the card, topmost first
    struct Below {
        WId id;
        QRect geometry;
    };
    QList<Below> below;
    for (int i = selfIndex - 1; i >= 0; --i) {
        const KWindowInfo info(stack.at(i), NET::WMGeometry | NET::WMDesktop | NET::WMState | NET::XAWMState);
        if (!info.valid() || info.isMinimized() || !info.isOnCurrentDesktop()) {
            continue;
        }
        if (info.geometry().intersects(card)) {
            below.append({stack.at(i), info.geometry()});
        }
    }
    if (below.isEmpty()) {
        return -1;
    }

    // sample a 3 x 8 grid of small patches; each patch comes from the topmost window below ours at that point
    const int patch = 6;
    qreal sum = 0;
    int count = 0;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 3; ++col) {
            const QPoint point(card.left() + card.width() * (2 * col + 1) / 6, card.top() + card.height() * (2 * row + 1) / 16);
            for (const Below &window : std::as_const(below)) {
                if (!window.geometry.adjusted(0, 0, -patch, -patch).contains(point)) {
                    continue;
                }
                const QImage image = screen->grabWindow(window.id, point.x() - window.geometry.x(), point.y() - window.geometry.y(), patch, patch)
                                         .toImage()
                                         .convertToFormat(QImage::Format_RGB32);
                if (!image.isNull()) {
                    qreal luminance = 0;
                    for (int y = 0; y < image.height(); ++y) {
                        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                        for (int x = 0; x < image.width(); ++x) {
                            luminance += 0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) + 0.0722 * qBlue(line[x]);
                        }
                    }
                    sum += luminance / (255.0 * image.width() * image.height());
                    ++count;
                }
                break;
            }
        }
    }
    return count > 0 ? sum / count : -1;
}

void DolphinDockWidget::updateGlass()
{
    if (!isVisible() || isFloating() || !window()->isVisible() || window()->isMinimized()) {
        return;
    }
    const qreal luminance = backdropLuminance();
    if (luminance < 0) {
        return;
    }
    static const int dark = neoEnvInt("DOLPHIN_NEO_GLASS_DARK", 90);
    static const int light = neoEnvInt("DOLPHIN_NEO_GLASS_LIGHT", 10);
    const qreal target = dark + (light - dark) * luminance;
    const qreal next = qAbs(target - m_glass) < 1.5 ? target : m_glass + (target - m_glass) * 0.45; // ease towards the target
    static const bool debug = qEnvironmentVariableIsSet("DOLPHIN_NEO_GLASS_DEBUG");
    if (debug) {
        qWarning("dolphin-neo glass [%s]: backdrop luminance %.2f -> target %.0f%%, now %.0f%%", qPrintable(windowTitle()), luminance, target, next);
    }
    if (!qFuzzyCompare(next, m_glass)) {
        m_glass = next;
        update();
    }
}

DolphinDockWidget::~DolphinDockWidget() = default;

void DolphinDockWidget::setLocked(bool lock)
{
    if (lock != m_locked) {
        m_locked = lock;

        if (lock) {
            if (!m_dockTitleBar) {
                m_dockTitleBar = new DolphinDockTitleBar(this);
            }
            setTitleBarWidget(m_dockTitleBar);
            setFeatures(QDockWidget::DockWidgetClosable);
        } else {
            setTitleBarWidget(nullptr);
            setFeatures(DefaultDockWidgetFeatures);
        }
    }
}

bool DolphinDockWidget::isLocked() const
{
    return m_locked;
}

// dolphin-neo: draw the docked panel as a floating "glass" card.
// DOLPHIN_NEO_SIDEBAR=plain restores the stock look.
void DolphinDockWidget::paintEvent(QPaintEvent *event)
{
    QDockWidget::paintEvent(event);
    if (neoGlassPlain() || isFloating()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(6, 6, -6, -6);
    const qreal radius = 12;

    painter.setPen(Qt::NoPen);
    const QScreen *currentScreen = screen();
    const QPixmap backdrop = (neoGlassImageMode() && currentScreen) ? neoBackdrop(currentScreen) : QPixmap();
    if (!backdrop.isNull()) {
        // static image: the part of the blurred wallpaper that lies behind the card on screen, under a window-coloured tint
        QPainterPath clip;
        clip.addRoundedRect(card, radius, radius);
        painter.save();
        painter.setClipPath(clip);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QPointF onScreen = QPointF(mapToGlobal(card.topLeft().toPoint()) - currentScreen->geometry().topLeft());
        const qreal factor = qreal(backdrop.width()) / currentScreen->geometry().width();
        painter.drawPixmap(card, backdrop, QRectF(onScreen * factor, card.size() * factor));
        QColor tint = palette().color(QPalette::Window);
        tint.setAlphaF(1.0 - qBound(0, neoSettingInt("GLASS", 55), 100) / 100.0); // how much of the picture shows
        painter.fillRect(card, tint);
        painter.restore();
    } else {
        // live blur: thin out the window tint under the card, so the compositor's blur shows through
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.setBrush(QColor(0, 0, 0, qRound(m_glass * 255 / 100)));
        painter.drawRoundedRect(card, radius, radius);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    // light veil, a little stronger at the top
    QLinearGradient veil(card.topLeft(), card.bottomLeft());
    veil.setColorAt(0.0, QColor(255, 255, 255, 24));
    veil.setColorAt(0.2, QColor(255, 255, 255, 10));
    veil.setColorAt(1.0, QColor(255, 255, 255, 6));
    painter.setBrush(veil);
    painter.drawRoundedRect(card, radius, radius);

    // rim: bright towards the top-left, fading along the diagonal, a faint return at the bottom-right
    QLinearGradient rim(card.topLeft(), card.bottomRight());
    rim.setColorAt(0.0, QColor(255, 255, 255, 105));
    rim.setColorAt(0.45, QColor(255, 255, 255, 22));
    rim.setColorAt(1.0, QColor(255, 255, 255, 55));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QBrush(rim), 1));
    painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    // disk usage of the home file system, at the bottom of the Places card (PlacesPanel leaves the room for it)
    if (objectName() == QLatin1String("placesDock")) {
        static QStorageInfo storage;
        static QElapsedTimer age;
        if (!age.isValid() || age.elapsed() > 15000) {
            storage = QStorageInfo(QDir::homePath());
            age.restart();
        }
        if (storage.isValid() && storage.bytesTotal() > 0 && card.height() > 200) {
            const QRectF box(card.left() + 8, card.bottom() - 8 - 54, card.width() - 16, 54);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 62));
            painter.drawRoundedRect(box, 10, 10);

            const qreal used = 1.0 - qreal(storage.bytesAvailable()) / qreal(storage.bytesTotal());
            QFont small = font();
            small.setPointSizeF(small.pointSizeF() * 0.86);
            painter.setFont(small);
            painter.setPen(palette().color(QPalette::WindowText));
            const QRectF textRect = box.adjusted(10, 8, -10, -24);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, KIO::convertSize(storage.bytesTotal()));
            painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, i18nc("@info:status Free disk space", "%1 free", KIO::convertSize(storage.bytesAvailable())));

            const QRectF groove(box.left() + 10, box.bottom() - 16, box.width() - 20, 6);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 44));
            painter.drawRoundedRect(groove, 3, 3);
            painter.setBrush(used > 0.9 ? QColor(224, 145, 79) : palette().color(QPalette::Highlight));
            painter.drawRoundedRect(QRectF(groove.topLeft(), QSizeF(qMax(6.0, groove.width() * used), groove.height())), 3, 3);
        }
    }
}

void DolphinDockWidget::timerEvent(QTimerEvent *event)
{
    QDockWidget::timerEvent(event);
    if (objectName() == QLatin1String("placesDock") && isVisible()) {
        update(); // the disk usage box re-reads the free space when it is older than 15 s
    }
}

bool DolphinDockWidget::eventFilter(QObject *watched, QEvent *event)
{
    // dolphin-neo: the static backdrop depends on where the window is on screen
    if (watched == window() && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        update();
    }
    return QDockWidget::eventFilter(watched, event);
}

bool DolphinDockWidget::event(QEvent *event)
{
    if ((event->type() == QEvent::Show || event->type() == QEvent::ParentChange) && !neoGlassPlain() && window() != this) {
        window()->removeEventFilter(this);
        window()->installEventFilter(this);
    }
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
        if (event->spontaneous()) {
            // The Dolphin window has been minimized or restored. We do not want this to be interpreted like a user was toggling the visibility of this widget.
            // We return here so no QDockWidget::visibilityChanged() signal is emitted. This does not seem to happen either way on Wayland.
            return true;
        }
        [[fallthrough]];
    default:
        return QDockWidget::event(event);
    }
}

#include "dolphindockwidget.moc"
#include "moc_dolphindockwidget.cpp"

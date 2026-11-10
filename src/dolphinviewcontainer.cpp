/*
 * SPDX-FileCopyrightText: 2007 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "dolphinviewcontainer.h"

#include "admin/bar.h"
#include "admin/workerintegration.h"
#include "dolphin_compactmodesettings.h"
#include "dolphin_contentdisplaysettings.h"
#include "dolphin_detailsmodesettings.h"
#include "dolphin_generalsettings.h"
#include "dolphin_iconsmodesettings.h"
#include "dolphindebug.h"
#include "dolphinplacesmodelsingleton.h"
#include "filterbar/filterbar.h"
#include "global.h"
#include "kitemviews/kfileitemmodel.h"
#include "kitemviews/kitemlistcontainer.h"
#include "kitemviews/kitemlistwidget.h"
#include "search/bar.h"
#include "selectionmode/topbar.h"
#include "statusbar/dolphinstatusbar.h"

#include <KActionCollection>
#include <KApplicationTrader>
#include <KFileItemActions>
#include <KFilePlacesModel>
#include <KIO/JobUiDelegateFactory>
#include <KIO/MkpathJob>
#include <KIO/ApplicationLauncherJob>
#include <QFileInfo>
#include <KMessageBox>
#include <KIO/CommandLauncherJob>
#include <KIO/OpenUrlJob>
#include <KIO/PreviewJob>
#include <KLocalizedString>
#include <KProtocolManager>
#include <KShell>
#include <kio_version.h>

#ifndef QT_NO_ACCESSIBILITY
#include <QAccessible>
#endif
#include <QApplication>
#include <QDesktopServices>
#include <QDropEvent>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>

#include <cmath>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QLocale>
#include <QStackedLayout>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMimeDatabase>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

bool isSearchUrl(const QUrl &url)
{
    return url.scheme().contains(QLatin1String("search"));
}

// An overview of the widgets contained by this ViewContainer
struct LayoutStructure {
    int searchBar = 0;
    int adminBar = 1;
    int messageWidget = 2;
    int selectionModeTopBar = 3;
    int sortBar = 4; // dolphin-ng
    int view = 5;
    int selectionModeBottomBar = 6;
    int filterBar = 7;
    int statusBar = 8;
};
constexpr LayoutStructure positionFor;

// dolphin-ng: a layout that puts its items one after the other and wraps to a new line when the width runs out
class NgFlowLayout : public QLayout
{
public:
    NgFlowLayout(QWidget *parent, int hSpacing, int vSpacing)
        : QLayout(parent)
        , m_hSpacing(hSpacing)
        , m_vSpacing(vSpacing)
    {
    }
    ~NgFlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0)) {
            delete item;
        }
    }
    void addItem(QLayoutItem *item) override
    {
        m_items.append(item);
    }
    int count() const override
    {
        return m_items.size();
    }
    QLayoutItem *itemAt(int index) const override
    {
        return m_items.value(index);
    }
    QLayoutItem *takeAt(int index) override
    {
        return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override
    {
        return {};
    }
    bool hasHeightForWidth() const override
    {
        return true;
    }
    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), true);
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }
    QSize sizeHint() const override
    {
        return minimumSize();
    }
    QSize minimumSize() const override
    {
        QSize size;
        for (const QLayoutItem *item : m_items) {
            size = size.expandedTo(item->minimumSize());
        }
        const QMargins margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
        int y = area.y();
        int i = 0;
        while (i < m_items.size()) {
            // take as many items as fit on this line (at least one)
            int x = area.x();
            int lineHeight = 0;
            int last = i;
            while (last < m_items.size()) {
                const QSize hint = m_items.at(last)->sizeHint();
                if (last > i && x + hint.width() > area.right() + 1) {
                    break;
                }
                x += hint.width() + m_hSpacing;
                lineHeight = qMax(lineHeight, hint.height());
                ++last;
            }
            if (!testOnly) {
                x = area.x();
                for (int k = i; k < last; ++k) {
                    const QSize hint = m_items.at(k)->sizeHint();
                    m_items.at(k)->setGeometry(QRect(x, y + (lineHeight - hint.height()) / 2, hint.width(), hint.height()));
                    x += hint.width() + m_hSpacing;
                }
            }
            y += lineHeight + m_vSpacing;
            i = last;
        }
        return y - m_vSpacing - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> m_items;
    int m_hSpacing;
    int m_vSpacing;
};

// dolphin-ng: the bar above the two-line list, which has no column header. On one line (wrapping when narrow):
// "Sort by" — one button per visible role, clicking the active one reverses the order;
// "Filtri" — a button with a menu of filters: all, files only, and one per kind of file, each with its count.
class NgSortBar : public QWidget
{
public:
    NgSortBar(DolphinView *view, QWidget *parent)
        : QWidget(parent)
        , m_view(view)
    {
        applyChipStyle();
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(14, 4, 14, 6);
        row->setSpacing(10);
        m_flow = new NgFlowLayout(nullptr, 4, 4);
        m_flow->setContentsMargins(0, 0, 0, 0);
        row->addLayout(m_flow, 1);

        // instant filter of the current folder: what you type narrows the list as you type
        m_filter = new QLineEdit(this);
        m_filter->setPlaceholderText(QStringLiteral("Cerca nella cartella"));
        m_filter->setClearButtonEnabled(true);
        m_filter->setFixedWidth(210);
        m_filter->setToolTip(QStringLiteral("Mostra solo gli elementi il cui nome contiene il testo. Esc svuota il campo."));
        row->addWidget(m_filter, 0, Qt::AlignTop);
        connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
            m_view->setNameFilter(text);
        });
        connect(view, &DolphinView::urlChanged, this, [this] {
            if (!m_filter->text().isEmpty()) {
                m_filter->clear(); // a new folder starts unfiltered
            }
        });

        connect(view, &DolphinView::sortRoleChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::sortOrderChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::visibleRolesChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::modeChanged, this, [this] {
            rebuild();
        });
        connect(view, &DolphinView::urlChanged, this, [this] {
            // a new folder starts unfiltered
            if (m_active != All) {
                m_active = All;
                m_view->setMimeTypeFilters({});
            }
        });
        connect(view, &DolphinView::directoryLoadingCompleted, this, [this] {
            count();
        });
        connect(view, &DolphinView::itemCountChanged, this, [this] {
            count();
        });
        count();
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::PaletteChange) {
            applyChipStyle();
        }
    }

private:
    // what the filter shows: everything, files only (no folders), or one NgKind::Category
    enum { All = -1, FilesOnly = NgKind::CategoryCount };

    // The chips and the search box draw themselves, the same under every widget style: a translucent
    // pill for the checked one, a fainter one on hover, no outlines that depend on the style's hover tracking.
    void applyChipStyle()
    {
        const QString ink = ngIsDark(palette()) ? QStringLiteral("255, 255, 255") : QStringLiteral("0, 0, 0");
        setStyleSheet(QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 3px 7px; background: transparent; color: palette(window-text); }"
                                     "QToolButton:hover { background: rgba(%1, 0.08); }"
                                     "QToolButton:pressed { background: rgba(%1, 0.14); }"
                                     "QToolButton:checked { background: rgba(%1, 0.16); border-color: rgba(%1, 0.20); }"
                                     "QToolButton:checked:hover { background: rgba(%1, 0.20); }"
                                     "QToolButton::menu-indicator { subcontrol-origin: padding; subcontrol-position: right center; left: -3px; }"
                                     "QLineEdit { border: 1px solid rgba(%1, 0.14); border-radius: 6px; padding: 3px 8px; background: rgba(%1, 0.06); color: palette(window-text); }"
                                     "QLineEdit:hover { border-color: rgba(%1, 0.22); }"
                                     "QLineEdit:focus { border-color: rgba(%1, 0.34); background: rgba(%1, 0.08); }")
                          .arg(ink));
    }

    QToolButton *chip(const QString &text, bool checked)
    {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setCheckable(true);
        button->setChecked(checked);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_flow->addWidget(button);
        return button;
    }

    void caption(const QString &text)
    {
        auto *label = new QLabel(text, this);
        label->setEnabled(false); // dimmed, it is only a caption
        label->setContentsMargins(0, 0, 6, 0);
        m_flow->addWidget(label);
    }

    void rebuild()
    {
        while (QLayoutItem *item = m_flow->takeAt(0)) {
            if (item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater(); // a button may be in the middle of its own click
            }
            delete item;
        }
        const bool list = m_view->viewMode() == DolphinView::DetailsView;

        // ---- sorting
        caption(i18nc("@action:inmenu View", "Sort By"));
        QList<QByteArray> roles = m_view->visibleRoles();
        roles.removeAll("text");
        roles.prepend("text");
        const QList<KFileItemModel::RoleInfo> rolesInfo = KFileItemModel::rolesInformation();
        const QByteArray sortRole = m_view->sortRole();
        for (const QByteArray &role : std::as_const(roles)) {
            QString text = QString::fromLatin1(role);
            for (const KFileItemModel::RoleInfo &info : rolesInfo) {
                if (info.role == role) {
                    text = info.translation;
                    break;
                }
            }
            QToolButton *button = chip(text, role == sortRole);
            if (role == sortRole) {
                button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                const bool ascending = m_view->sortOrder() == Qt::AscendingOrder;
                button->setIcon(QIcon::fromTheme(ascending ? QStringLiteral("view-sort-ascending") : QStringLiteral("view-sort-descending")));
            }
            connect(button, &QToolButton::clicked, this, [this, role] {
                if (m_view->sortRole() == role) {
                    m_view->setSortOrder(m_view->sortOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder);
                } else {
                    m_view->setSortRole(role);
                }
            });
        }

        // ---- filters
        if (m_total == 0 || m_total > 4000) {
            m_flow->invalidate();
            return;
        }
        auto *gap = new QWidget(this);
        gap->setFixedWidth(22);
        m_flow->addWidget(gap);

        // one button that opens a menu with the filters; its label says which one is active
        static const char *const labels[NgKind::CategoryCount] = {"Cartelle", "Immagini", "Documenti", "Fogli", "Archivi", "Audio e video", "Codice e dati", "Pacchetti", "Altro"};
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(QIcon::fromTheme(QStringLiteral("view-filter")));
        button->setPopupMode(QToolButton::InstantPopup);
        button->setCheckable(true);
        button->setChecked(m_active != All); // highlighted while something is filtered out
        auto *menu = new QMenu(button);
        auto *group = new QActionGroup(menu);
        QString current = QStringLiteral("Tutti");
        auto entry = [&](const QString &label, int number, int what) {
            QAction *action = menu->addAction(QStringLiteral("%1\t%2").arg(label).arg(number));
            action->setCheckable(true);
            action->setChecked(m_active == what);
            group->addAction(action);
            if (m_active == what) {
                current = label;
            }
            connect(action, &QAction::triggered, this, [this, what] {
                setActive(what);
            });
        };
        entry(QStringLiteral("Tutti"), m_total, All);
        const int folders = m_counts[NgKind::Folder];
        if (folders > 0 && folders < m_total) {
            entry(QStringLiteral("File"), m_total - folders, FilesOnly);
        }
        menu->addSeparator();
        for (int i = 0; i < NgKind::CategoryCount; ++i) {
            if (m_counts[i] > 0) {
                entry(QString::fromUtf8(labels[i]), m_counts[i], i);
            }
        }
        button->setMenu(menu);
        button->setText(QStringLiteral("Filtri: %1").arg(current));
        m_flow->addWidget(button);

        if (!list) {
            m_flow->invalidate();
            return;
        }
        // row style: cards or open rows; the choice is remembered
        const bool open = ngRowStyle() == NgRowStyle::Open;
        auto *rows = new QToolButton(this);
        rows->setAutoRaise(true);
        rows->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        rows->setIcon(QIcon::fromTheme(open ? QStringLiteral("view-list-text") : QStringLiteral("view-list-details")));
        rows->setText(open ? QStringLiteral("Righe: aperte") : QStringLiteral("Righe: schede"));
        connect(rows, &QToolButton::clicked, this, [this, open] {
            setNgRowStyle(open ? NgRowStyle::Card : NgRowStyle::Open);
            const QList<QGraphicsView *> graphicsViews = m_view->findChildren<QGraphicsView *>();
            for (QGraphicsView *graphicsView : graphicsViews) {
                graphicsView->viewport()->update();
            }
            rebuild();
        });
        m_flow->addWidget(rows);
        m_flow->invalidate();
    }

    // Count the items of each kind. Only while no filter is active: a filtered view no longer lists the rest.
    void count()
    {
        if (m_active != All) {
            return;
        }
        const KFileItemList items = m_view->items();
        for (int i = 0; i < NgKind::CategoryCount; ++i) {
            m_counts[i] = 0;
            m_mimeTypes[i].clear();
        }
        m_total = items.count();
        if (m_total <= 4000) { // asking thousands of files for their type would stall the window
            for (const KFileItem &item : items) {
                const NgKind::Category category = ngKindFor(item.text(), item.isDir()).category;
                ++m_counts[category];
                const QString mimeType = item.mimetype();
                if (!m_mimeTypes[category].contains(mimeType)) {
                    m_mimeTypes[category].append(mimeType);
                }
            }
        }
        rebuild();
    }

    void setActive(int what)
    {
        m_active = what;
        QStringList mimeTypes;
        if (what == FilesOnly) {
            for (int i = 0; i < NgKind::CategoryCount; ++i) {
                if (i != NgKind::Folder) {
                    mimeTypes += m_mimeTypes[i];
                }
            }
            mimeTypes.removeDuplicates();
        } else if (what != All) {
            mimeTypes = m_mimeTypes[what];
        }
        m_view->setMimeTypeFilters(mimeTypes);
        rebuild();
    }

    DolphinView *m_view;
    NgFlowLayout *m_flow;
    QLineEdit *m_filter;
    int m_active = All;
    int m_total = 0;
    int m_counts[NgKind::CategoryCount] = {};
    QStringList m_mimeTypes[NgKind::CategoryCount];
};

// dolphin-ng: a picture that fits its area, zooms with the mouse wheel around the pointer and pans by dragging
class NgZoomImage : public QWidget
{
public:
    explicit NgZoomImage(QWidget *parent)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        setMinimumSize(200, 120);
    }

    void setPixmap(const QPixmap &pixmap, bool keepView = false)
    {
        m_pixmap = pixmap;
        if (!keepView) {
            m_zoom = 1.0;
            m_offset = QPointF();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        if (m_pixmap.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(target(), m_pixmap, QRectF(m_pixmap.rect()));
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_pixmap.isNull()) {
            return;
        }
        const qreal oldZoom = m_zoom;
        m_zoom = qBound(1.0, m_zoom * std::pow(1.2, event->angleDelta().y() / 120.0), 12.0);
        // keep the point under the pointer where it is
        const QPointF fromCentre = event->position() - QPointF(width(), height()) / 2.0;
        m_offset = fromCentre - (fromCentre - m_offset) * (m_zoom / oldZoom);
        clampOffset();
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_dragFrom = event->position();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            m_offset += event->position() - m_dragFrom;
            m_dragFrom = event->position();
            clampOffset();
            update();
        }
    }

private:
    QSizeF fitted() const
    {
        return QSizeF(m_pixmap.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio) * m_zoom;
    }
    QRectF target() const
    {
        const QSizeF s = fitted();
        return QRectF(QPointF((width() - s.width()) / 2.0, (height() - s.height()) / 2.0) + m_offset, s);
    }
    void clampOffset()
    {
        const QSizeF s = fitted();
        const qreal maxX = qMax(0.0, (s.width() - width()) / 2.0);
        const qreal maxY = qMax(0.0, (s.height() - height()) / 2.0);
        m_offset = QPointF(qBound(-maxX, m_offset.x(), maxX), qBound(-maxY, m_offset.y(), maxY));
    }

    QPixmap m_pixmap;
    qreal m_zoom = 1.0;
    QPointF m_offset;
    QPointF m_dragFrom;
};

// dolphin-ng: a card with a preview of one item and its main actions, shown over the list right under the item's row.
// Text files show their first lines, folders their first entries, everything else the thumbnail KIO can make.
class NgPreviewCard : public QWidget
{
public:
    NgPreviewCard(DolphinView *view, QWidget *parent)
        : QWidget(parent)
        , m_view(view)
    {
        // a tall card: the preview on top, then name and details, then the buttons (two per row)
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(8);

        m_preview = new QLabel(this);
        m_preview->setTextFormat(Qt::PlainText);
        m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_preview->setMinimumSize(200, 120);
        m_image = new NgZoomImage(this);
        m_stack = new QStackedLayout();
        m_stack->addWidget(m_preview);
        m_stack->addWidget(m_image);
        layout->addLayout(m_stack, 1);

        m_title = new QLabel(this);
        QFont bold = m_title->font();
        bold.setBold(true);
        m_title->setFont(bold);
        m_meta = new QLabel(this);
        m_meta->setWordWrap(true);
        m_meta->setEnabled(false); // dimmed
        layout->addWidget(m_title);
        layout->addWidget(m_meta);

        m_buttons = new QGridLayout();
        m_buttons->setSpacing(6);
        layout->addLayout(m_buttons);
        addButton(QStringLiteral("Apri"), [this] {
            Q_EMIT m_view->itemActivated(m_item);
        })->setDefault(true);
        addButton(QStringLiteral("Apri con…"), [this] {
            auto *job = new KIO::ApplicationLauncherJob(); // no application given: asks which one
            job->setUrls({m_item.url()});
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
            job->start();
        });
        addButton(QStringLiteral("Rinomina"), [this] {
            m_view->renameSelectedItems(); // the controller selected the item before asking for the preview
        });
        addButton(QStringLiteral("Copia percorso"), [this] {
            QGuiApplication::clipboard()->setText(m_item.isLocalFile() ? m_item.localPath() : m_item.url().toDisplayString());
        });
        m_fixedButtons = m_buttonCount;

        setMouseTracking(true); // edge cursors
        qApp->installEventFilter(this);
        hide();
    }

    QPushButton *addButton(const QString &text, std::function<void()> action, bool keepOpen = false)
    {
        auto *button = new QPushButton(text, this);
        connect(button, &QPushButton::clicked, this, [this, action, keepOpen] {
            if (!keepOpen) {
                hide();
            }
            action();
        });
        m_buttons->addWidget(button, m_buttonCount / 2, m_buttonCount % 2);
        ++m_buttonCount;
        return button;
    }

    // Runs a command line tool in the folder of the item; the folder view picks the result up by itself.
    void run(const QString &program, const QStringList &arguments)
    {
        if (QStandardPaths::findExecutable(program).isEmpty()) {
            KMessageBox::error(window(), QStringLiteral("Serve il programma «%1», che non è installato.").arg(program));
            return;
        }
        auto *job = new KIO::CommandLauncherJob(program, arguments);
        job->setWorkingDirectory(m_item.url().adjusted(QUrl::RemoveFilename).toLocalFile());
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, window()));
        job->start();
    }

    // a button that opens a menu of choices; `choices` are (label, arguments for `program`) pairs
    void addMenuButton(const QString &text, const QString &program, const QList<std::pair<QString, QStringList>> &choices)
    {
        QPushButton *button = addButton(text, [] {}, true);
        auto *menu = new QMenu(button);
        for (const auto &[label, arguments] : choices) {
            menu->addAction(label, this, [this, program, arguments] {
                hide();
                run(program, arguments);
            });
        }
        button->setMenu(menu);
        connect(menu, &QMenu::aboutToHide, this, [this] {
            // menu closed without a choice while the card was opened by hovering: close it unless the mouse is back on it
            QTimer::singleShot(300, this, [this] {
                if (m_hover && isVisible() && !underMouse() && !QApplication::activePopupWidget()) {
                    hide();
                }
            });
        });
    }

    // The buttons that make sense for this kind of file (images, archives, PDF, audio, video).
    void rebuildTypeButtons()
    {
        while (m_buttonCount > m_fixedButtons) {
            --m_buttonCount;
            QLayoutItem *item = m_buttons->itemAtPosition(m_buttonCount / 2, m_buttonCount % 2);
            QWidget *widget = item ? item->widget() : nullptr; // removeWidget() frees the item itself
            if (widget) {
                m_buttons->removeWidget(widget);
                widget->hide();
                widget->deleteLater();
            }
        }
        if (!m_item.isLocalFile() || m_item.isDir()) {
            return;
        }
        const QString path = m_item.localPath();
        const QFileInfo info(path);
        const QString base = info.completeBaseName();
        const QString suffix = info.suffix();
        const QString mime = m_item.mimetype();
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
        if (isImage) {
            addMenuButton(QStringLiteral("Ridimensiona…"),
                          QStringLiteral("magick"),
                          {{QStringLiteral("75 %"), {path, QStringLiteral("-resize"), QStringLiteral("75%"), QStringLiteral("%1-75.%2").arg(base, suffix)}},
                           {QStringLiteral("50 %"), {path, QStringLiteral("-resize"), QStringLiteral("50%"), QStringLiteral("%1-50.%2").arg(base, suffix)}},
                           {QStringLiteral("25 %"), {path, QStringLiteral("-resize"), QStringLiteral("25%"), QStringLiteral("%1-25.%2").arg(base, suffix)}},
                           {QStringLiteral("Larghezza 1920"), {path, QStringLiteral("-resize"), QStringLiteral("1920x>"), QStringLiteral("%1-1920.%2").arg(base, suffix)}},
                           {QStringLiteral("Larghezza 1280"), {path, QStringLiteral("-resize"), QStringLiteral("1280x>"), QStringLiteral("%1-1280.%2").arg(base, suffix)}},
                           {QStringLiteral("Larghezza 800"), {path, QStringLiteral("-resize"), QStringLiteral("800x>"), QStringLiteral("%1-800.%2").arg(base, suffix)}}});
            QList<std::pair<QString, QStringList>> formats;
            for (const char *format : {"png", "jpg", "webp", "avif", "tiff", "bmp"}) {
                if (QString::fromLatin1(format) != suffix.toLower() && !(suffix.toLower() == QLatin1String("jpeg") && QLatin1String(format) == QLatin1String("jpg"))) {
                    formats.append({QString::fromLatin1(format).toUpper(), {path, QStringLiteral("%1.%2").arg(base, QString::fromLatin1(format))}});
                }
            }
            addMenuButton(QStringLiteral("Converti in…"), QStringLiteral("magick"), formats);
        } else if (archives.contains(mime)) {
            addButton(QStringLiteral("Estrai qui in sottocartella"), [this, path] {
                if (!QStandardPaths::findExecutable(QStringLiteral("ark")).isEmpty()) {
                    run(QStringLiteral("ark"), {QStringLiteral("--batch"), QStringLiteral("--autosubfolder"), path});
                } else {
                    const QString folder = QFileInfo(path).completeBaseName();
                    QDir(QFileInfo(path).absolutePath()).mkdir(folder);
                    run(QStringLiteral("bsdtar"), {QStringLiteral("-xf"), path, QStringLiteral("-C"), folder});
                }
            });
        } else if (mime == QLatin1String("application/pdf")) {
            addButton(QStringLiteral("Estrai il testo (TXT)"), [this, path, base] {
                run(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), path, QStringLiteral("%1.txt").arg(base)});
            });
            addButton(QStringLiteral("Pagine in immagini (PNG)"), [this, path, base] {
                run(QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("150"), path, base});
            });
        } else if (mime.startsWith(QLatin1String("audio/"))) {
            if (suffix.toLower() != QLatin1String("mp3")) {
                addButton(QStringLiteral("Converti in MP3"), [this, path, base] {
                    run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
                });
            }
            addButton(QStringLiteral("Converti in FLAC"), [this, path, base] {
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("%1.flac").arg(base)});
            });
        } else if (mime.startsWith(QLatin1String("video/"))) {
            addButton(QStringLiteral("Converti in MP4 (H.264)"), [this, path, base] {
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("%1.mp4").arg(base)});
            });
            addButton(QStringLiteral("Estrai l'audio (MP3)"), [this, path, base] {
                run(QStringLiteral("ffmpeg"), {QStringLiteral("-i"), path, QStringLiteral("-vn"), QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
            });
        }
    }

    // Opened by hovering a row (PREVIEW_HOVER): the card goes away when the mouse leaves both the row and the card.
    void showForHover(const KFileItem &item, const QRect &rowRect)
    {
        if (isVisible() && item.url() == m_item.url()) {
            return;
        }
        m_hiddenAt.invalidate();
        showFor(item, rowRect);
        m_hover = true;
    }

    void hoverEnded()
    {
        if (!m_hover || !isVisible()) {
            return;
        }
        // give the mouse a moment to reach the card
        QTimer::singleShot(260, this, [this] {
            if (m_hover && isVisible() && !underMouse() && !QApplication::activePopupWidget()) {
                hide();
            }
        });
    }

    void showFor(const KFileItem &item, const QRect &rowRect)
    {
        m_hover = false;
        // a second click on the same preview button closes the card (the press already hid it)
        if (m_hiddenAt.isValid() && m_hiddenAt.elapsed() < 400 && item.url() == m_item.url()) {
            m_hiddenAt.invalidate();
            return;
        }
        m_item = item;
        m_title->setText(m_title->fontMetrics().elidedText(item.text(), Qt::ElideMiddle, 420));
        m_title->setToolTip(item.text());
        m_meta->setText(QStringLiteral("%1 · %2 · %3").arg(item.mimeComment(), item.isDir() ? QString() : KIO::convertSize(item.size()), QLocale().toString(item.time(KFileItem::ModificationTime), QLocale::ShortFormat)));
        rebuildTypeButtons();
        fillPreview();

        QWidget *host = parentWidget();
        // the size the user dragged it to last time (PREVIEW_SIZE=WxH), else a tall card
        int width = 460;
        int height = 470;
        const QStringList saved = ngSetting("PREVIEW_SIZE").split(QLatin1Char('x'));
        if (saved.size() == 2 && saved.at(0).toInt() >= MinimumWidth && saved.at(1).toInt() >= MinimumHeight) {
            width = saved.at(0).toInt();
            height = saved.at(1).toInt();
        }
        width = qMin(width, qMax(MinimumWidth, host->width() - 8));
        height = qMin(height, qMax(MinimumHeight, host->height() - 8));
        const QPoint below = host->mapFromGlobal(rowRect.bottomLeft());
        int y = below.y() + 2;
        if (y + height > host->height() - 6) {
            y = host->mapFromGlobal(rowRect.topLeft()).y() - height - 2; // no room below: open above the row
        }
        setGeometry(below.x() + 3, qMax(4, y), width, height);
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor window = palette().color(QPalette::Window);
        QColor fill = ngIsDark(palette()) ? window.lighter(118) : window.darker(104);
        QColor border = palette().color(QPalette::WindowText);
        border.setAlphaF(0.16);
        painter.setPen(QPen(border, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
        // resize grip: three short diagonal strokes in the bottom-right corner
        QColor grip = palette().color(QPalette::WindowText);
        grip.setAlphaF(0.35);
        painter.setPen(QPen(grip, 1.2));
        const QPointF corner(width() - 5, height() - 5);
        for (int i = 0; i < 3; ++i) {
            const qreal d = 4 + i * 4;
            painter.drawLine(QPointF(corner.x() - d, corner.y()), QPointF(corner.x(), corner.y() - d));
        }
    }

    // ---- resizing by the right / bottom edge or the corner
    Qt::Edges edgesAt(const QPoint &pos) const
    {
        Qt::Edges edges;
        if (pos.x() >= width() - EdgeWidth) {
            edges |= Qt::RightEdge;
        }
        if (pos.y() >= height() - EdgeWidth) {
            edges |= Qt::BottomEdge;
        }
        if (QRect(width() - 18, height() - 18, 18, 18).contains(pos)) {
            edges = Qt::RightEdge | Qt::BottomEdge;
        }
        return edges;
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_resizing = event->button() == Qt::LeftButton ? edgesAt(event->pos()) : Qt::Edges();
        if (m_resizing) {
            m_resizeStart = event->globalPosition().toPoint();
            m_resizeSize = size();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_resizing) {
            const QPoint delta = event->globalPosition().toPoint() - m_resizeStart;
            QSize next = m_resizeSize;
            if (m_resizing & Qt::RightEdge) {
                next.setWidth(m_resizeSize.width() + delta.x());
            }
            if (m_resizing & Qt::BottomEdge) {
                next.setHeight(m_resizeSize.height() + delta.y());
            }
            const QWidget *host = parentWidget();
            next.setWidth(qBound(MinimumWidth, next.width(), qMax(MinimumWidth, host->width() - x() - 4)));
            next.setHeight(qBound(MinimumHeight, next.height(), qMax(MinimumHeight, host->height() - y() - 4)));
            resize(next);
            return;
        }
        const Qt::Edges edges = edgesAt(event->pos());
        setCursor(edges == (Qt::RightEdge | Qt::BottomEdge) ? Qt::SizeFDiagCursor
                      : edges & Qt::RightEdge               ? Qt::SizeHorCursor
                      : edges & Qt::BottomEdge              ? Qt::SizeVerCursor
                                                            : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_resizing) {
            m_resizing = Qt::Edges();
            setNgSetting("PREVIEW_SIZE", QStringLiteral("%1x%2").arg(width()).arg(height())); // remembered for the next time
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (isVisible()) {
            if (event->type() == QEvent::MouseButtonPress) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (QApplication::activePopupWidget()) {
                    // a click in a menu of the card (or any popup) is not a click elsewhere
                } else if (!QRect(mapToGlobal(QPoint(0, 0)), size()).contains(mouse->globalPosition().toPoint())) {
                    close(); // a click anywhere else closes the card; the click still does its job
                }
            } else if (event->type() == QEvent::Wheel) {
                const auto *wheel = static_cast<QWheelEvent *>(event);
                if (!QRect(mapToGlobal(QPoint(0, 0)), size()).contains(wheel->globalPosition().toPoint())) {
                    close(); // scrolling the list closes the card; over the card the wheel zooms the picture
                }
            } else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                close();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        if (m_hover && isVisible()) {
            // left the card: if the mouse is not back on a row, close; a row re-opens it after its delay
            QTimer::singleShot(120, this, [this] {
                if (m_hover && isVisible() && !underMouse() && !QApplication::activePopupWidget()) {
                    hide(); // (a menu of the card holds the mouse: not a leave)
                }
            });
        }
    }

    void hideEvent(QHideEvent *event) override
    {
        m_hiddenAt.start();
        QWidget::hideEvent(event);
    }

private:
    void fillPreview()
    {
        m_stack->setCurrentWidget(m_preview);
        m_preview->setPixmap(QPixmap());
        m_preview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        m_preview->setFont(font());
        const QString path = m_item.localPath();

        if (m_item.isDir() && !path.isEmpty()) {
            const QStringList entries = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
            QStringList lines = entries.mid(0, 9);
            if (entries.size() > lines.size()) {
                lines.append(QStringLiteral("… altri %1").arg(entries.size() - lines.size()));
            }
            m_preview->setText(lines.isEmpty() ? QStringLiteral("(cartella vuota)") : lines.join(QLatin1Char('\n')));
            return;
        }

        const QMimeType mimeType = QMimeDatabase().mimeTypeForName(m_item.mimetype());
        if (!path.isEmpty() && mimeType.isValid() && mimeType.inherits(QStringLiteral("text/plain"))) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QStringList lines = QString::fromUtf8(file.read(8000)).split(QLatin1Char('\n')).mid(0, 9);
                for (QString &line : lines) {
                    line = line.left(140);
                }
                m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
                m_preview->setText(lines.join(QLatin1Char('\n')));
                return;
            }
        }

        // everything else: the type icon at once, then a thumbnail from KIO, then (pictures only) the picture itself,
        // so that zooming in shows real detail
        m_stack->setCurrentWidget(m_image);
        m_image->setPixmap(QIcon::fromTheme(m_item.iconName()).pixmap(96, 96));
        const QUrl url = m_item.url();
        KIO::PreviewJob *job = KIO::filePreview(KFileItemList{m_item}, QSize(1024, 640));
        job->setIgnoreMaximumSize(m_item.isLocalFile());
        connect(job, &KIO::PreviewJob::gotPreview, this, [this, url](const KFileItem &, const QPixmap &pixmap) {
            if (isVisible() && m_item.url() == url && !pixmap.isNull() && !m_fullPicture) {
                m_image->setPixmap(pixmap);
            }
        });
        m_fullPicture = false;
        if (!path.isEmpty() && m_item.mimetype() == QLatin1String("application/pdf")) {
            // a PDF thumbnail is too coarse to zoom into: render the first page properly with pdftoppm (poppler-utils)
            static const QString pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
            if (!pdftoppm.isEmpty()) {
                auto *dir = new QTemporaryDir();
                auto *process = new QProcess(this);
                const QString base = dir->filePath(QStringLiteral("page"));
                connect(process, &QProcess::finished, this, [this, url, process, dir, base](int exitCode) {
                    if (exitCode == 0 && isVisible() && m_item.url() == url) {
                        const QPixmap page(base + QStringLiteral(".png"));
                        if (!page.isNull()) {
                            m_fullPicture = true;
                            m_image->setPixmap(page, true);
                        }
                    }
                    process->deleteLater();
                    delete dir;
                });
                process->start(pdftoppm, {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("200"), QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("1"), QStringLiteral("-singlefile"), path, base});
            }
        }
        if (!path.isEmpty() && m_item.mimetype().startsWith(QLatin1String("image/")) && m_item.size() < 80 * 1024 * 1024) {
            QTimer::singleShot(150, this, [this, url, path] {
                if (!isVisible() || m_item.url() != url) {
                    return;
                }
                QImageReader reader(path);
                reader.setAutoTransform(true);
                const QSize original = reader.size();
                if (original.isValid() && qMax(original.width(), original.height()) > 3200) {
                    reader.setScaledSize(original.scaled(3200, 3200, Qt::KeepAspectRatio)); // enough to zoom, without eating memory
                }
                const QImage image = reader.read();
                if (!image.isNull()) {
                    m_fullPicture = true;
                    m_image->setPixmap(QPixmap::fromImage(image), true);
                }
            });
        }
    }

    DolphinView *m_view;
    KFileItem m_item;
    QLabel *m_preview;
    NgZoomImage *m_image;
    QStackedLayout *m_stack;
    bool m_fullPicture = false;
    QLabel *m_title;
    QLabel *m_meta;
    QGridLayout *m_buttons;
    static constexpr int MinimumWidth = 320;
    static constexpr int MinimumHeight = 300;
    static constexpr int EdgeWidth = 8;
    Qt::Edges m_resizing;
    QPoint m_resizeStart;
    QSize m_resizeSize;
    int m_buttonCount = 0;
    int m_fixedButtons = 0; // the first ones are for every file; the rest depend on its type
    QElapsedTimer m_hiddenAt;
    bool m_hover = false; // opened by resting the mouse on the row
};

DolphinViewContainer::DolphinViewContainer(const QUrl &url, QWidget *parent)
    : QWidget(parent)
    , m_topLayout(nullptr)
    , m_urlNavigator{new DolphinUrlNavigator(url)}
    , m_urlNavigatorConnected{nullptr}
    , m_searchBar(nullptr)
    , m_searchModeEnabled(false)
    , m_adminBar{nullptr}
    , m_authorizeToEnterFolderAction{nullptr}
    , m_createFolderAction(nullptr)
    , m_messageWidget(nullptr)
    , m_selectionModeTopBar{nullptr}
    , m_view(nullptr)
    , m_filterBar(nullptr)
    , m_selectionModeBottomBar{nullptr}
    , m_statusBar(nullptr)
    , m_statusBarTimer(nullptr)
    , m_statusBarTimestamp()
    , m_grabFocusOnUrlChange{true}
{
    hide();

    m_topLayout = new QGridLayout(this);
    m_topLayout->setSpacing(0);
    m_topLayout->setContentsMargins(0, 0, 0, 0);

    m_messageWidget = new KMessageWidget(this);
    m_messageWidget->setCloseButtonVisible(true);
    m_messageWidget->setPosition(KMessageWidget::Header);
    m_messageWidget->hide();

#if !defined(Q_OS_WIN) && !defined(Q_OS_HAIKU)
    if (getuid() == 0) {
        // We must be logged in as the root user; show a big scary warning
        showMessage(i18n("Running Dolphin as root can be dangerous. Please be careful."), KMessageWidget::Warning);
    }
#endif

    // Initialize filter bar
    m_filterBar = new FilterBar(this);
    m_filterBar->setVisible(GeneralSettings::filterBar(), WithoutAnimation);

    connect(m_filterBar, &FilterBar::filterChanged, this, &DolphinViewContainer::setNameFilter);
    connect(m_filterBar, &FilterBar::filterModeChanged, this, &DolphinViewContainer::setFilterMode);
    connect(m_filterBar, &FilterBar::caseSensitiveChanged, this, &DolphinViewContainer::setFilterCaseSensitive);
    connect(m_filterBar, &FilterBar::closeRequest, this, &DolphinViewContainer::closeFilterBar);
    connect(m_filterBar, &FilterBar::focusViewRequest, this, &DolphinViewContainer::requestFocus);

    // Initialize the main view
    m_view = new DolphinView(url, this);
    connect(m_view, &DolphinView::urlChanged, m_filterBar, &FilterBar::clearIfUnlocked);
    connect(m_view, &DolphinView::urlChanged, m_messageWidget, &KMessageWidget::hide);
    // m_urlNavigator stays in sync with m_view's location changes and
    // keeps track of them so going back and forth in the history works.
    connect(m_view, &DolphinView::urlChanged, m_urlNavigator.get(), &DolphinUrlNavigator::setLocationUrl);
    connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlChanged, this, &DolphinViewContainer::slotUrlNavigatorLocationChanged);
    connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlAboutToBeChanged, this, &DolphinViewContainer::slotUrlNavigatorLocationAboutToBeChanged);
    connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlSelectionRequested, this, &DolphinViewContainer::slotUrlSelectionRequested);
    connect(m_view, &DolphinView::writeStateChanged, this, &DolphinViewContainer::writeStateChanged);
    connect(m_view, &DolphinView::requestItemInfo, this, &DolphinViewContainer::showItemInfo);
    connect(m_view, &DolphinView::itemActivated, this, &DolphinViewContainer::slotItemActivated);
    connect(m_view, &DolphinView::fileMiddleClickActivated, this, &DolphinViewContainer::slotfileMiddleClickActivated);
    connect(m_view, &DolphinView::itemsActivated, this, &DolphinViewContainer::slotItemsActivated);
    connect(m_view, &DolphinView::redirection, this, &DolphinViewContainer::redirect);
    connect(m_view, &DolphinView::directoryLoadingStarted, this, &DolphinViewContainer::slotDirectoryLoadingStarted);
    connect(m_view, &DolphinView::directoryLoadingCompleted, this, &DolphinViewContainer::slotDirectoryLoadingCompleted);
    connect(m_view, &DolphinView::directoryLoadingCanceled, this, &DolphinViewContainer::slotDirectoryLoadingCanceled);
    connect(m_view, &DolphinView::itemCountChanged, this, &DolphinViewContainer::delayedStatusBarUpdate);
    connect(m_view, &DolphinView::selectionChanged, this, &DolphinViewContainer::delayedStatusBarUpdate);
    connect(m_view, &DolphinView::errorMessage, this, &DolphinViewContainer::slotErrorMessageFromView);
    connect(m_view, &DolphinView::urlIsFileError, this, &DolphinViewContainer::slotUrlIsFileError);
    connect(m_view, &DolphinView::activated, this, &DolphinViewContainer::activate);
    connect(m_view, &DolphinView::hiddenFilesShownChanged, this, &DolphinViewContainer::slotHiddenFilesShownChanged);
    connect(m_view, &DolphinView::sortHiddenLastChanged, this, &DolphinViewContainer::slotSortHiddenLastChanged);
    connect(m_view, &DolphinView::currentDirectoryRemoved, this, &DolphinViewContainer::slotCurrentDirectoryRemoved);

    // Initialize status bar
    m_statusBar = new DolphinStatusBar(this);
    m_statusBar->setUrl(m_view->url());
    m_statusBar->setZoomLevel(m_view->zoomLevel());
    connect(m_view, &DolphinView::urlChanged, m_statusBar, &DolphinStatusBar::setUrl);
    connect(m_view, &DolphinView::zoomLevelChanged, m_statusBar, &DolphinStatusBar::setZoomLevel);
    connect(m_view, &DolphinView::infoMessage, m_statusBar, &DolphinStatusBar::setTemporaryRichText);
    connect(m_view, &DolphinView::operationCompletedMessage, m_statusBar, &DolphinStatusBar::setTemporaryRichText);
    connect(m_view, &DolphinView::showTypeAheadFeedback, m_statusBar, &DolphinStatusBar::setTemporaryRichText);
    connect(m_view, &DolphinView::statusBarTextChanged, m_statusBar, &DolphinStatusBar::setDefaultText);
    connect(m_view, &DolphinView::directoryLoadingProgress, m_statusBar, [this](int percent) {
        m_statusBar->showProgress(i18nc("@info:progress", "Loading folder…"), percent);
    });
    connect(m_view, &DolphinView::directorySortingProgress, m_statusBar, [this](int percent) {
        m_statusBar->showProgress(i18nc("@info:progress", "Sorting…"), percent);
    });
    connect(m_statusBar, &DolphinStatusBar::stopPressed, this, &DolphinViewContainer::stopDirectoryLoading);
    connect(m_statusBar, &DolphinStatusBar::zoomLevelChanged, this, &DolphinViewContainer::slotStatusBarZoomLevelChanged);
    connect(m_statusBar, &DolphinStatusBar::showMessage, this, [this](const QString &message, KMessageWidget::MessageType messageType) {
        showMessage(message, messageType);
    });
    connect(m_statusBar, &DolphinStatusBar::widthUpdated, this, &DolphinViewContainer::updateStatusBarGeometry);
    connect(m_statusBar, &DolphinStatusBar::urlChanged, this, &DolphinViewContainer::updateStatusBar);
    connect(this, &DolphinViewContainer::showFilterBarChanged, this, &DolphinViewContainer::updateStatusBar);

    m_statusBarTimer = new QTimer(this);
    m_statusBarTimer->setSingleShot(true);
    m_statusBarTimer->setInterval(300);
    connect(m_statusBarTimer, &QTimer::timeout, this, &DolphinViewContainer::updateStatusBar);

    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    connect(undoManager, &KIO::FileUndoManager::jobRecordingFinished, this, &DolphinViewContainer::delayedStatusBarUpdate);

    m_topLayout->addWidget(m_messageWidget, positionFor.messageWidget, 0);
    {
        // dolphin-ng: the sort/filter bar and the preview card belong to the two-line list
        auto *sortBar = new NgSortBar(m_view, this);
        m_topLayout->addWidget(sortBar, positionFor.sortBar, 0);
        sortBar->setVisible(ngTwoLineList());
        auto *previewCard = new NgPreviewCard(m_view, this);
        connect(m_view, &DolphinView::ngPreviewRequested, previewCard, [previewCard](const KFileItem &item, const QRect &rowRect) {
            previewCard->showFor(item, rowRect);
        });
        connect(m_view, &DolphinView::ngHoverPreviewRequested, previewCard, [previewCard](const KFileItem &item, const QRect &rowRect) {
            previewCard->showForHover(item, rowRect);
        });
        connect(m_view, &DolphinView::ngHoverPreviewEnded, previewCard, [previewCard] {
            previewCard->hoverEnded();
        });
        connect(m_view, &DolphinView::urlChanged, previewCard, &QWidget::hide);
        connect(NgSettings::instance(), &NgSettings::changed, sortBar, [sortBar] {
            sortBar->setVisible(ngTwoLineList());
        });
    }
    m_topLayout->addWidget(m_view, positionFor.view, 0);
    m_topLayout->addWidget(m_filterBar, positionFor.filterBar, 0);
    if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::FullWidth) {
        m_topLayout->addWidget(m_statusBar, positionFor.statusBar, 0);
    }
    connect(m_statusBar, &DolphinStatusBar::modeUpdated, this, [this]() {
        const bool statusBarInLayout = m_topLayout->itemAtPosition(positionFor.statusBar, 0);
        if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::FullWidth) {
            if (!statusBarInLayout) {
                m_topLayout->addWidget(m_statusBar, positionFor.statusBar, 0);
                m_statusBar->setUrl(m_view->url());
            }
        } else {
            if (statusBarInLayout) {
                m_topLayout->removeWidget(m_statusBar);
            }
        }
        updateStatusBarGeometry();
    });
    m_statusBar->setHidden(false);

    setSearchBarVisible(isSearchUrl(url));

    // Update view as the ContentDisplaySettings change
    // this happens here and not in DolphinView as DolphinviewContainer and DolphinView are not in the same build target ATM
    connect(ContentDisplaySettings::self(), &KCoreConfigSkeleton::configChanged, m_view, &DolphinView::reload);

    KFilePlacesModel *placesModel = DolphinPlacesModelSingleton::instance().placesModel();
    connect(placesModel, &KFilePlacesModel::dataChanged, this, &DolphinViewContainer::slotPlacesModelChanged);
    connect(placesModel, &KFilePlacesModel::rowsInserted, this, &DolphinViewContainer::slotPlacesModelChanged);
    connect(placesModel, &KFilePlacesModel::rowsRemoved, this, &DolphinViewContainer::slotPlacesModelChanged);

    QApplication::instance()->installEventFilter(this);

    // Update the view with the current state of the filter bar (from the state config)
    m_view->setFilterMode(m_filterBar->filterMode());
    m_view->setFilterCaseSensitive(m_filterBar->isCaseSensitive());
}

DolphinViewContainer::~DolphinViewContainer() = default;

QUrl DolphinViewContainer::url() const
{
    return m_view->url();
}

KFileItem DolphinViewContainer::rootItem() const
{
    return m_view->rootItem();
}

void DolphinViewContainer::setActive(bool active)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setActive(active);
    }
    m_view->setActive(active);
}

bool DolphinViewContainer::isActive() const
{
    return m_view->isActive();
}

void DolphinViewContainer::setGrabFocusOnUrlChange(bool grabFocus)
{
    m_grabFocusOnUrlChange = grabFocus;
}

const DolphinUrlNavigator *DolphinViewContainer::urlNavigator() const
{
    return m_urlNavigatorConnected;
}

DolphinUrlNavigator *DolphinViewContainer::urlNavigator()
{
    return m_urlNavigatorConnected;
}

const DolphinUrlNavigator *DolphinViewContainer::urlNavigatorInternalWithHistory() const
{
    return m_urlNavigator.get();
}

DolphinUrlNavigator *DolphinViewContainer::urlNavigatorInternalWithHistory()
{
    return m_urlNavigator.get();
}

const DolphinView *DolphinViewContainer::view() const
{
    return m_view;
}

DolphinView *DolphinViewContainer::view()
{
    return m_view;
}

void DolphinViewContainer::connectUrlNavigator(DolphinUrlNavigator *urlNavigator)
{
    Q_ASSERT(urlNavigator);
    Q_ASSERT(!m_urlNavigatorConnected);
    Q_ASSERT(m_urlNavigator.get() != urlNavigator);
    Q_ASSERT(m_view);

    urlNavigator->setLocationUrl(m_view->url());
    urlNavigator->setShowHiddenFolders(m_view->hiddenFilesShown());
    urlNavigator->setSortHiddenFoldersLast(m_view->sortHiddenLast());
    if (m_urlNavigatorVisualState) {
        urlNavigator->setVisualState(*m_urlNavigatorVisualState.get());
        m_urlNavigatorVisualState.reset();
    }
    urlNavigator->setActive(isActive());

    // Url changes are still done via m_urlNavigator.
    connect(urlNavigator, &DolphinUrlNavigator::urlChanged, m_urlNavigator.get(), &DolphinUrlNavigator::setLocationUrl);
    connect(urlNavigator, &DolphinUrlNavigator::urlsDropped, this, [=, this](const QUrl &destination, QDropEvent *event) {
        m_view->dropUrls(destination, event, urlNavigator->dropWidget());
    });
    // Aside from these, only visual things need to be connected.
    connect(m_view, &DolphinView::urlChanged, urlNavigator, &DolphinUrlNavigator::setLocationUrl);
    connect(urlNavigator, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);
    connect(urlNavigator, &DolphinUrlNavigator::requestToLoseFocus, m_view, [this]() {
        m_view->setFocus();
    });

    urlNavigator->setReadOnlyBadgeVisible(rootItem().isLocalFile() && !rootItem().isWritable());

    m_urlNavigatorConnected = urlNavigator;
}

void DolphinViewContainer::disconnectUrlNavigator()
{
    if (!m_urlNavigatorConnected) {
        return;
    }

    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::urlChanged, m_urlNavigator.get(), &DolphinUrlNavigator::setLocationUrl);
    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::urlsDropped, this, nullptr);
    disconnect(m_view, &DolphinView::urlChanged, m_urlNavigatorConnected, &DolphinUrlNavigator::setLocationUrl);
    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);
    disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::requestToLoseFocus, m_view, nullptr);

    m_urlNavigatorVisualState = m_urlNavigatorConnected->visualState();
    m_urlNavigatorConnected = nullptr;
}

void DolphinViewContainer::setSearchBarVisible(bool visible)
{
    if (!visible) {
        if (isSearchBarVisible()) {
            m_searchBar->setVisible(false, WithAnimation, Search::Bar::HideBehavior::KeepCurrentUrl);
        }
        return;
    }

    if (!m_searchBar) {
        m_searchBar = new Search::Bar(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), QUrl{} /** will be set below. */), this);
        connect(m_searchBar, &Search::Bar::urlChangeRequested, this, [this](const QUrl &url) {
            m_view->setViewPropertiesContext(isSearchUrl(url) ? QStringLiteral("search") : QString());
            setGrabFocusOnUrlChange(false); // Prevent loss of focus while typing or refining a search.
            setUrl(url);
            setGrabFocusOnUrlChange(true);
        });
        connect(m_searchBar, &Search::Bar::focusViewRequest, this, &DolphinViewContainer::requestFocus);
        connect(m_searchBar, &Search::Bar::showMessage, this, [this](const QString &message, KMessageWidget::MessageType messageType) {
            showMessage(message, messageType);
        });
        connect(m_searchBar,
                &Search::Bar::showInstallationProgress,
                m_statusBar,
                [this](const QString &currentlyRunningTaskTitle, int installationProgressPercent) {
                    m_statusBar->showProgress(currentlyRunningTaskTitle, installationProgressPercent, DolphinStatusBar::CancelLoading::Disallowed);
                });
        connect(m_searchBar, &Search::Bar::visibilityChanged, this, &DolphinViewContainer::searchBarVisibilityChanged);
        m_topLayout->addWidget(m_searchBar, positionFor.searchBar, 0);
    }

    m_searchBar->setVisible(true, WithAnimation);

    // The Search::Bar has been set visible but its state does not yet match with this view container or view.
    // The view might for example already be searching because it was opened with a search URL. The Search::Bar needs to be updated to show the parameters of
    // that search. And even if there is no search URL loaded in the view currently, we still need to figure out where the Search::Bar should be searching if
    // the user starts a search from there. Let's figure out the search location in this method and let the DolphinQuery constructor figure out the rest from
    // the current m_urlNavigator->locationUrl().
    for (int i = m_urlNavigator->historyIndex(); i < m_urlNavigator->historySize(); i++) {
        QUrl url = m_urlNavigator->locationUrl(i);
        if (isSearchUrl(url)) {
            // The previous location was a search URL. Try to see if that search URL has a valid search path so we keep searching in the same location.
            const auto searchPath = Search::DolphinQuery(url, QUrl{}).searchPath(); // DolphinQuery is great at extracting the search path from a search URL.
            if (searchPath.isValid()) {
                m_searchBar->updateStateToMatch(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), searchPath));
                return;
            }
        } else if (url.scheme() == QLatin1String("tags")) {
            continue; // We avoid setting a tags url as the backup search path because a DolphinQuery constructed from a tags url will already search tagged
                      // items everywhere.
        } else {
            m_searchBar->updateStateToMatch(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), url));
            return;
        }
    }
    // We could not find any URL fit for searching in the history. This might happen because this view only ever loaded a search which searches "Everywhere"
    // and therefore there is no specific search path to choose from. But the Search::Bar *needs* to know a search path because the user might switch from
    // searching "Everywhere" to "Here" and it is everybody's guess what "Here" is supposed to mean in that context… We'll simply fall back to the user's home
    // path for lack of a better option.
    m_searchBar->updateStateToMatch(std::make_shared<const Search::DolphinQuery>(m_urlNavigator->locationUrl(), QUrl::fromUserInput(QDir::homePath())));
}

bool DolphinViewContainer::isSearchBarVisible() const
{
    return m_searchBar && m_searchBar->isVisible() && m_searchBar->isEnabled();
}

void DolphinViewContainer::setFocusToSearchBar()
{
    Q_ASSERT(isSearchBarVisible());
    m_searchBar->selectAll();
}

void DolphinViewContainer::setSelectionModeEnabled(bool enabled, KActionCollection *actionCollection, SelectionMode::BottomBar::Contents bottomBarContents)
{
    const bool wasEnabled = m_view->selectionMode();
    m_view->setSelectionModeEnabled(enabled);

    if (!enabled) {
        if (!wasEnabled) {
            return; // nothing to do here
        }
        Q_ASSERT(m_selectionModeTopBar); // there is no point in disabling selectionMode when it wasn't even enabled once.
        Q_ASSERT(m_selectionModeBottomBar);
        m_selectionModeTopBar->setVisible(false, WithAnimation);
        m_selectionModeBottomBar->setVisible(false, WithAnimation);
        Q_EMIT selectionModeChanged(false);

        if (!QApplication::focusWidget() || m_selectionModeTopBar->isAncestorOf(QApplication::focusWidget())
            || m_selectionModeBottomBar->isAncestorOf(QApplication::focusWidget())) {
            m_view->setFocus();
        }
        return;
    }

    if (!m_selectionModeTopBar) {
        // Changing the location will disable selection mode.
        connect(m_urlNavigator.get(), &DolphinUrlNavigator::urlChanged, this, [this]() {
            setSelectionModeEnabled(false);
        });

        m_selectionModeTopBar = new SelectionMode::TopBar(this); // will be created hidden
        connect(m_selectionModeTopBar, &SelectionMode::TopBar::selectionModeLeavingRequested, this, [this]() {
            setSelectionModeEnabled(false);
        });
        m_topLayout->addWidget(m_selectionModeTopBar, positionFor.selectionModeTopBar, 0);
    }

    if (!m_selectionModeBottomBar) {
        m_selectionModeBottomBar = new SelectionMode::BottomBar(actionCollection, this);
        connect(m_view, &DolphinView::selectionChanged, this, [this](const KFileItemList &selection) {
            m_selectionModeBottomBar->slotSelectionChanged(selection, m_view->url());
        });
        connect(m_selectionModeBottomBar, &SelectionMode::BottomBar::error, this, &DolphinViewContainer::showErrorMessage);
        connect(m_selectionModeBottomBar, &SelectionMode::BottomBar::selectionModeLeavingRequested, this, [this]() {
            setSelectionModeEnabled(false);
        });
        m_topLayout->addWidget(m_selectionModeBottomBar, positionFor.selectionModeBottomBar, 0);
    }
    m_selectionModeBottomBar->resetContents(bottomBarContents);
    if (bottomBarContents == SelectionMode::BottomBar::GeneralContents) {
        m_selectionModeBottomBar->slotSelectionChanged(m_view->selectedItems(), m_view->url());
    }

    if (!wasEnabled) {
        m_selectionModeTopBar->setVisible(true, WithAnimation);
        m_selectionModeBottomBar->setVisible(true, WithAnimation);
        Q_EMIT selectionModeChanged(true);
    }
}

bool DolphinViewContainer::isSelectionModeEnabled() const
{
    const bool isEnabled = m_view->selectionMode();
    Q_ASSERT((!isEnabled
              // We can't assert that the bars are invisible only because the selection mode is disabled because the hide animation might still be playing.
              && (!m_selectionModeBottomBar || !m_selectionModeBottomBar->isEnabled() || !m_selectionModeBottomBar->isVisible()
                  || m_selectionModeBottomBar->contents() == SelectionMode::BottomBar::PasteContents))
             || (isEnabled && m_selectionModeTopBar
                 && m_selectionModeTopBar->isVisible()
                 // The bottom bar is either visible or was hidden because it has nothing to show in GeneralContents mode e.g. because no items are selected.
                 && m_selectionModeBottomBar
                 && (m_selectionModeBottomBar->isVisible() || m_selectionModeBottomBar->contents() == SelectionMode::BottomBar::GeneralContents)));
    return isEnabled;
}

void DolphinViewContainer::slotSplitTabDisabled()
{
    if (m_selectionModeBottomBar) {
        m_selectionModeBottomBar->slotSplitTabDisabled();
    }
}

void DolphinViewContainer::showMessage(const QString &message, KMessageWidget::MessageType messageType, std::initializer_list<QAction *> buttonActions)
{
    if (message.isEmpty()) {
        return;
    }

    m_messageWidget->setText(message);

    // TODO: wrap at arbitrary character positions once QLabel can do this
    // https://bugreports.qt.io/browse/QTBUG-1276
    m_messageWidget->setWordWrap(true);
    m_messageWidget->setMessageType(messageType);

    const QList<QAction *> previousMessageWidgetActions = m_messageWidget->actions();
    for (auto action : previousMessageWidgetActions) {
        m_messageWidget->removeAction(action);
    }
    for (QAction *action : buttonActions) {
        m_messageWidget->addAction(action);
    }

    m_messageWidget->setWordWrap(false);
    const int unwrappedWidth = m_messageWidget->sizeHint().width();
    m_messageWidget->setWordWrap(unwrappedWidth > size().width());

    if (m_messageWidget->isVisible()) {
        m_messageWidget->hide();
    }
    m_messageWidget->animatedShow();

#ifndef QT_NO_ACCESSIBILITY
    if (QAccessible::isActive() && isActive()) {
        // To announce the new message keyboard focus must be moved to the message label. However, we do not have direct access to the label that is internal
        // to the KMessageWidget. Instead we setFocus() on the KMessageWidget and trust that it has set correct focus handling.
        m_messageWidget->setFocus();
    }
#endif
}

void DolphinViewContainer::showProgress(const QString &currentlyRunningTaskTitle, int progressPercent)
{
    m_statusBar->showProgress(currentlyRunningTaskTitle, progressPercent, DolphinStatusBar::CancelLoading::Disallowed);
}

void DolphinViewContainer::readSettings()
{
    // The startup settings should (only) get applied if they have been
    // modified by the user. Otherwise keep the (possibly) different current
    // setting of the filterbar.
    if (GeneralSettings::modifiedStartupSettings()) {
        setFilterBarVisible(GeneralSettings::filterBar());
    }

    m_view->readSettings();
    m_statusBar->readSettings();
}

bool DolphinViewContainer::isFilterBarVisible() const
{
    return m_filterBar->isEnabled(); // Gets disabled in AnimatedHeightWidget while animating towards a hidden state.
}

QString DolphinViewContainer::placesText() const
{
    QString text;

    if (isSearchBarVisible() && m_searchBar->isSearchConfigured()) {
        text = m_searchBar->queryTitle();
    } else {
        text = url().adjusted(QUrl::StripTrailingSlash).fileName();
        if (text.isEmpty()) {
            text = url().host();
        }
        if (text.isEmpty()) {
            text = url().scheme();
        }
    }

    return text;
}

void DolphinViewContainer::reload()
{
    view()->reload();
    m_messageWidget->hide();
    m_statusBar->updateSpaceInfo();
}

QString DolphinViewContainer::captionWindowTitle() const
{
    if (GeneralSettings::showFullPathInTitlebar() && (!isSearchBarVisible() || !m_searchBar->isSearchConfigured())) {
        if (!url().isLocalFile()) {
            return url().adjusted(QUrl::StripTrailingSlash).toString();
        }
        return url().adjusted(QUrl::StripTrailingSlash).path();
    } else {
        return DolphinViewContainer::caption();
    }
}

QString DolphinViewContainer::caption() const
{
    // see KUrlNavigatorPrivate::firstButtonText().
    if (url().path().isEmpty() || url().path() == QLatin1Char('/')) {
        QUrlQuery query(url());
        const QString title = query.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded);
        if (!title.isEmpty()) {
            return title;
        }
    }

    if (isSearchBarVisible() && m_searchBar->isSearchConfigured()) {
        return m_searchBar->queryTitle();
    }

    KFilePlacesModel *placesModel = DolphinPlacesModelSingleton::instance().placesModel();

    QModelIndex url_index = placesModel->closestItem(url());

    if (url_index.isValid() && placesModel->url(url_index).matches(url(), QUrl::StripTrailingSlash)) {
        return placesModel->text(url_index);
    }

    if (!url().isLocalFile()) {
        QUrl adjustedUrl = url().adjusted(QUrl::StripTrailingSlash);
        QString caption;
        if (!adjustedUrl.fileName().isEmpty()) {
            caption = adjustedUrl.fileName();
        } else if (!adjustedUrl.path().isEmpty() && adjustedUrl.path() != "/") {
            caption = adjustedUrl.path();
        } else if (!adjustedUrl.host().isEmpty()) {
            caption = adjustedUrl.host();
        } else {
            caption = adjustedUrl.toString();
        }
        return caption;
    }

    QString fileName = url().adjusted(QUrl::StripTrailingSlash).fileName();
    if (fileName.isEmpty()) {
        fileName = '/';
    }

    return fileName;
}

void DolphinViewContainer::setUrl(const QUrl &newUrl)
{
    if (newUrl != m_urlNavigator->locationUrl()) {
        m_urlNavigator->setLocationUrl(newUrl);
        if (m_searchBar && !Search::isSupportedSearchScheme(newUrl.scheme())) {
            m_searchBar->setSearchPath(newUrl);
        }
    }
}

void DolphinViewContainer::setFilterBarVisible(bool visible)
{
    Q_ASSERT(m_filterBar);
    if (visible) {
        m_view->hideToolTip(ToolTipManager::HideBehavior::Instantly);
        m_filterBar->setVisible(true, WithAnimation);
        m_filterBar->setFocus();
        m_filterBar->selectAll();
        Q_EMIT showFilterBarChanged(true);
    } else {
        closeFilterBar();
    }
}

void DolphinViewContainer::delayedStatusBarUpdate()
{
    if (m_statusBarTimer->isActive() && (m_statusBarTimestamp.elapsed() > 2000)) {
        // No update of the statusbar has been done during the last 2 seconds,
        // although an update has been requested. Trigger an immediate update.
        m_statusBarTimer->stop();
        updateStatusBar();
    } else {
        // Invoke updateStatusBar() with a small delay. This assures that
        // when a lot of delayedStatusBarUpdates() are done in a short time,
        // no bottleneck is given.
        m_statusBarTimer->start();
    }
}

void DolphinViewContainer::updateStatusBar()
{
    m_statusBarTimestamp.start();
    m_view->requestStatusBarText();
    updateStatusBarGeometry();
}

void DolphinViewContainer::slotDirectoryLoadingStarted()
{
    if (isSearchUrl(url())) {
        // Search KIO-slaves usually don't provide any progress information. Give
        // a hint to the user that a searching is done:
        updateStatusBar();
        m_statusBar->showProgress(i18nc("@info", "Searching…"), -1);
    } else {
        // Trigger an undetermined progress indication. The progress
        // information in percent will be triggered by the percent() signal
        // of the directory lister later.
        m_statusBar->showProgress(QString(), -1);
    }

    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setReadOnlyBadgeVisible(false);
    }
}

void DolphinViewContainer::slotDirectoryLoadingCompleted()
{
    m_statusBar->showProgress(QString(), 100);

    if (isSearchUrl(url()) && m_view->itemsCount() == 0) {
        // The dir lister has been completed on a Baloo-URI and no items have been found. Instead
        // of showing the default status bar information ("0 items") a more helpful information is given:
        m_statusBar->setDefaultText(i18nc("@info:status", "No items found."));
    } else {
        updateStatusBar();
    }

    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setReadOnlyBadgeVisible(rootItem().isLocalFile() && !rootItem().isWritable());
    }

    // Update admin bar visibility
    if (m_view->url().scheme() == QStringLiteral("admin")) {
        if (!m_adminBar) {
            m_adminBar = new Admin::Bar(this);
            m_topLayout->addWidget(m_adminBar, positionFor.adminBar, 0);
        }
        m_adminBar->setVisible(true, WithAnimation);
    } else if (m_adminBar) {
        m_adminBar->setVisible(false, WithAnimation);
    }
}

void DolphinViewContainer::slotDirectoryLoadingCanceled()
{
    m_statusBar->showProgress(QString(), 100);
}

void DolphinViewContainer::slotUrlIsFileError(const QUrl &url)
{
    const KFileItem item(url);

    setUrl(KIO::upUrl(item.url()));

    if (!m_openAsFile) {
        m_openAsFile = [this, item] {
            auto action = new QAction(i18nc("@action", "Open as File"), this);
            connect(action, &QAction::triggered, this, [this, item]() {
                m_messageWidget->animatedHide();

                // Find out if the file can be opened in the view (for example, this is the
                // case if the file is an archive). The mime type must be known for that.
                item.determineMimeType();
                const QUrl &folderUrl = DolphinView::openItemAsFolderUrl(item, true);
                if (!folderUrl.isEmpty()) {
                    setUrl(folderUrl);
                } else {
                    slotItemActivated(item);
                }
            });
            return action;
        }();
    }
    showMessage(xi18nc("@info", "Trying to open a file as a folder: <filename>%1</filename>", item.url().toString()), KMessageWidget::Error, {m_openAsFile});
}

void DolphinViewContainer::slotItemActivated(const KFileItem &item)
{
    // It is possible to activate items on inactive views by
    // drag & drop operations. Assure that activating an item always
    // results in an active view.
    m_view->setActive(true);

    const QUrl &url = DolphinView::openItemAsFolderUrl(item, GeneralSettings::browseThroughArchives());
    if (!url.isEmpty()) {
        const auto modifiers = QGuiApplication::keyboardModifiers();
        // keep in sync with KUrlNavigator::slotNavigatorButtonClicked
        if (modifiers & Qt::ControlModifier && modifiers & Qt::ShiftModifier) {
            Q_EMIT activeTabRequested(url);
        } else if (modifiers & Qt::ControlModifier) {
            Q_EMIT tabRequested(url);
        } else if (modifiers & Qt::ShiftModifier) {
            Dolphin::openNewWindow({KFilePlacesModel::convertedUrl(url)}, this);
        } else {
            setUrl(url);
        }
        return;
    }

    KIO::OpenUrlJob *job = new KIO::OpenUrlJob(item.targetUrl(), item.mimetype());
    // Auto*Warning*Handling, errors are put in a KMessageWidget by us in slotOpenUrlFinished.
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoWarningHandlingEnabled, this));
    job->setShowOpenOrExecuteDialog(true);
    connect(job, &KIO::OpenUrlJob::finished, this, &DolphinViewContainer::slotOpenUrlFinished);
    job->start();
}

void DolphinViewContainer::slotfileMiddleClickActivated(const KFileItem &item)
{
    KService::List services = KApplicationTrader::queryByMimeType(item.mimetype());
    const auto modifiers = QGuiApplication::keyboardModifiers();

    int indexOfAppToOpenFileWith = 1;
    if (modifiers & Qt::ShiftModifier) {
        indexOfAppToOpenFileWith = 2;
    }

    // executable scripts
    auto mimeType = item.currentMimeType();
    if (item.isLocalFile() && mimeType.inherits(QStringLiteral("application/x-executable")) && mimeType.inherits(QStringLiteral("text/plain"))
        && item.isExecutable()) {
        KConfigGroup cfgGroup(KSharedConfig::openConfig(QStringLiteral("kiorc")), QStringLiteral("Executable scripts"));
        const QString value = cfgGroup.readEntry("behaviourOnLaunch", "alwaysAsk");

        // in case KIO::WidgetsOpenOrExecuteFileHandler::promptUserOpenOrExecute would not open the file
        if (value != QLatin1String("open")) {
            --indexOfAppToOpenFileWith;
        }
    }

    if (services.length() >= indexOfAppToOpenFileWith + 1) {
        const auto &service = services.at(indexOfAppToOpenFileWith);

        KIO::ApplicationLauncherJob *job = new KIO::ApplicationLauncherJob(service, this);
        job->setUrls({item.targetUrl()});

        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
        connect(job, &KIO::OpenUrlJob::finished, this, &DolphinViewContainer::slotOpenUrlFinished);
        job->start();
    } else {
        // If no 2nd or 3rd service available, try to open archives in new tabs, regardless of the "Browse compressed files as folders" setting.
        const QUrl &url = DolphinView::openItemAsFolderUrl(item);
        if (!url.isEmpty()) {
            // keep in sync with KUrlNavigator::slotNavigatorButtonClicked
            if (modifiers & Qt::ShiftModifier) {
                Q_EMIT activeTabRequested(url);
            } else {
                Q_EMIT tabRequested(url);
            }
        }
    }
}

void DolphinViewContainer::slotItemsActivated(const KFileItemList &items)
{
    Q_ASSERT(items.count() >= 2);

    KFileItemActions fileItemActions(this);
    fileItemActions.runPreferredApplications(items);
}

void DolphinViewContainer::showItemInfo(const KFileItem &item)
{
    if (item.isNull()) {
        m_statusBar->setHoveredItemText(QString());
    } else {
        m_statusBar->setHoveredItemText(item.getStatusBarInfo());
    }
}

void DolphinViewContainer::closeFilterBar()
{
    m_filterBar->closeFilterBar();
    m_view->setFocus();
    Q_EMIT showFilterBarChanged(false);
}

void DolphinViewContainer::clearFilterBar()
{
    m_filterBar->clearIfUnlocked();
}

void DolphinViewContainer::setNameFilter(const QString &nameFilter)
{
    m_view->hideToolTip(ToolTipManager::HideBehavior::Instantly);
    m_view->setNameFilter(nameFilter);
    delayedStatusBarUpdate();
}

void DolphinViewContainer::setFilterMode(const KFileItemModelFilter::FilterMode mode)
{
    m_view->setFilterMode(mode);
    delayedStatusBarUpdate();
}

void DolphinViewContainer::setFilterCaseSensitive(const bool caseSensitive)
{
    m_view->setFilterCaseSensitive(caseSensitive);
    delayedStatusBarUpdate();
}

void DolphinViewContainer::activate()
{
    setActive(true);
}

void DolphinViewContainer::slotUrlNavigatorLocationAboutToBeChanged(const QUrl &)
{
    saveViewState();
}

void DolphinViewContainer::slotUrlNavigatorLocationChanged(const QUrl &url)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->slotReturnPressed();
    }

    if (KProtocolManager::supportsListing(url)) {
        if (isSearchUrl(url)) {
            setSearchBarVisible(true);
        } else if (m_searchBar && m_searchBar->isSearchConfigured()) {
            // Hide the search bar because it shows an outdated search which the user does not care about anymore.
            setSearchBarVisible(false);
        }

        m_view->setUrl(url);
        tryRestoreViewState();

        if (m_grabFocusOnUrlChange && isActive()) {
            // When an URL has been entered, the view should get the focus.
            // The focus must be requested asynchronously, as changing the URL might create
            // a new view widget.
            QTimer::singleShot(0, this, &DolphinViewContainer::requestFocus);
        }
    } else if (KProtocolManager::isSourceProtocol(url)) {
        if (url.scheme().startsWith(QLatin1String("http"))) {
            showMessage(i18nc("@info:status", // krazy:exclude=qmethods
                              "Dolphin does not support web pages, the web browser has been launched"),
                        KMessageWidget::Information);
        } else {
            showMessage(i18nc("@info:status", "Protocol not supported by Dolphin, default application has been launched"), KMessageWidget::Information);
        }

        QDesktopServices::openUrl(url);
        redirect(QUrl(), m_urlNavigator->locationUrl(1));
    } else {
        if (!url.scheme().isEmpty()) {
            showMessage(i18nc("@info:status", "Invalid protocol '%1'", url.scheme()), KMessageWidget::Error);
        } else {
            showMessage(i18nc("@info:status", "Invalid protocol"), KMessageWidget::Error);
        }
        m_urlNavigator->goBack();
    }
}

void DolphinViewContainer::slotUrlSelectionRequested(const QUrl &url)
{
    m_view->markUrlsAsSelected({url});
    m_view->markUrlAsCurrent(url); // makes the item scroll into view
}

void DolphinViewContainer::disableUrlNavigatorSelectionRequests()
{
    disconnect(m_urlNavigator.get(), &KUrlNavigator::urlSelectionRequested, this, &DolphinViewContainer::slotUrlSelectionRequested);
}

void DolphinViewContainer::enableUrlNavigatorSelectionRequests()
{
    connect(m_urlNavigator.get(), &KUrlNavigator::urlSelectionRequested, this, &DolphinViewContainer::slotUrlSelectionRequested);
}

void DolphinViewContainer::redirect(const QUrl &oldUrl, const QUrl &newUrl)
{
    Q_UNUSED(oldUrl)
    const bool block = m_urlNavigator->signalsBlocked();
    m_urlNavigator->blockSignals(true);

    // Assure that the location state is reset for redirection URLs. This
    // allows to skip redirection URLs when going back or forward in the
    // URL history.
    m_urlNavigator->saveLocationState(QByteArray());
    m_urlNavigator->setLocationUrl(newUrl);
    setSearchBarVisible(isSearchUrl(newUrl));

    m_urlNavigator->blockSignals(block);

    // Before emitting `urlChanged`, temporarily disconnect the `activated` signal to avoid activation of `DolphinViewContainer`.
    bool blockActivation = m_urlNavigatorConnected && !isActive();
    if (blockActivation) {
        disconnect(m_urlNavigatorConnected, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);
    }

    Q_EMIT m_view->urlChanged(newUrl);

    if (blockActivation) {
        connect(m_urlNavigatorConnected, &DolphinUrlNavigator::activated, this, &DolphinViewContainer::activate);

        // Force inactivate `DolphinUrlNavigator`.
        m_urlNavigatorConnected->setActive(false);
    }
}

void DolphinViewContainer::requestFocus()
{
    m_view->setFocus();
}

void DolphinViewContainer::stopDirectoryLoading()
{
    m_view->stopLoading();
    m_statusBar->showProgress(QString(), 100);
}

void DolphinViewContainer::slotStatusBarZoomLevelChanged(int zoomLevel)
{
    m_view->setZoomLevel(zoomLevel);
}

bool DolphinViewContainer::isTopMostExistingParentFolderWritable(QUrl url)
{
    Q_ASSERT(url.isLocalFile());

    while (!url.toLocalFile().isEmpty()) {
        url = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);

        QFileInfo info(url.toLocalFile());

        if (info.exists()) {
            return info.isWritable();
        } else if (info.isSymLink()) {
            return false;
        }
    }
    return false;
}

void DolphinViewContainer::slotErrorMessageFromView(const QString &message, const int kioErrorCode)
{
    if (kioErrorCode == KIO::ERR_CANNOT_ENTER_DIRECTORY && m_view->url().scheme() == QStringLiteral("file")
        && KProtocolInfo::isKnownProtocol(QStringLiteral("admin")) && !rootItem().isReadable()) {
        // Explain to users that they need authentication to see the folder contents.
        if (!m_authorizeToEnterFolderAction) { // This code is similar to parts of Admin::Bar::hideTheNextTimeAuthorizationExpires().
            // We should not simply use the actAsAdminAction() itself here because that one always refers to the active view instead of this->m_view.
            auto actAsAdminAction = Admin::WorkerIntegration::FriendAccess::actAsAdminAction();
            m_authorizeToEnterFolderAction = new QAction{actAsAdminAction->icon(), actAsAdminAction->text(), this};
            m_authorizeToEnterFolderAction->setToolTip(actAsAdminAction->toolTip());
            m_authorizeToEnterFolderAction->setWhatsThis(actAsAdminAction->whatsThis());
            connect(m_authorizeToEnterFolderAction, &QAction::triggered, this, [this, actAsAdminAction]() {
                setActive(true);
                actAsAdminAction->trigger();
            });
        }
        showMessage(i18nc("@info", "Authorization required to enter this folder."), KMessageWidget::Error, {m_authorizeToEnterFolderAction});
        return;
    } else if (kioErrorCode == KIO::ERR_DOES_NOT_EXIST && m_view->url().isLocalFile()) {
        if (!m_createFolderAction) {
            m_createFolderAction = new QAction(this);
            m_createFolderAction->setText(i18nc("@action", "Create missing folder"));
            m_createFolderAction->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
            connect(m_createFolderAction, &QAction::triggered, this, [this](bool) {
                KIO::MkpathJob *job = KIO::mkpath(m_view->url());
                KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkpath, {}, m_view->url(), job);
                connect(job, &KJob::result, this, [this](KJob *job) {
                    if (job->error()) {
                        showErrorMessage(job->errorString());
                    } else {
                        m_view->reload();
                        m_messageWidget->animatedHide();
                    }
                });
            });
        }
        if (isTopMostExistingParentFolderWritable(m_view->url())) {
            m_createFolderAction->setEnabled(true);
            m_createFolderAction->setToolTip(i18nc("@info:tooltip", "Create the folder at this path and open it"));
        } else {
            m_createFolderAction->setEnabled(false);
            m_createFolderAction->setToolTip(i18nc("@info:tooltip", "You do not have permission to create the folder"));
        }
        showMessage(message, KMessageWidget::Error, {m_createFolderAction});
        return;
    }
    Q_EMIT showErrorMessage(message);
}

void DolphinViewContainer::showErrorMessage(const QString &message)
{
    showMessage(message, KMessageWidget::Error);
}

void DolphinViewContainer::slotPlacesModelChanged()
{
    if (!GeneralSettings::showFullPathInTitlebar()) {
        Q_EMIT captionChanged();
    }
}

void DolphinViewContainer::slotHiddenFilesShownChanged(bool showHiddenFiles)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setShowHiddenFolders(showHiddenFiles);
    }
}

void DolphinViewContainer::slotSortHiddenLastChanged(bool hiddenLast)
{
    if (m_urlNavigatorConnected) {
        m_urlNavigatorConnected->setSortHiddenFoldersLast(hiddenLast);
    }
}

void DolphinViewContainer::slotCurrentDirectoryRemoved()
{
    const QString location(url().toDisplayString(QUrl::PreferLocalFile));
    if (url().isLocalFile()) {
        const QString dirPath = url().toLocalFile();
        const QString newPath = getNearestExistingAncestorOfPath(dirPath);
        const QUrl newUrl = QUrl::fromLocalFile(newPath);
        // #473377: Delay changing the url to avoid modifying KCoreDirLister before KCoreDirListerCache::deleteDir() returns.
        QTimer::singleShot(0, this, [&, newUrl, location] {
            setUrl(newUrl);
            showMessage(xi18n("Current location changed, <filename>%1</filename> is no longer accessible.", location), KMessageWidget::Warning);
        });
    } else
        showMessage(xi18n("Current location changed, <filename>%1</filename> is no longer accessible.", location), KMessageWidget::Warning);
}

void DolphinViewContainer::slotOpenUrlFinished(KJob *job)
{
    if (job->error() && job->error() != KIO::ERR_USER_CANCELED) {
        showErrorMessage(job->errorString());
    }
}

void DolphinViewContainer::saveViewState()
{
    QByteArray locationState;
    QDataStream stream(&locationState, QIODevice::WriteOnly);
    m_view->saveState(stream);
    m_urlNavigator->saveLocationState(locationState);
}

void DolphinViewContainer::tryRestoreViewState()
{
    QByteArray locationState = m_urlNavigator->locationState();
    if (!locationState.isEmpty()) {
        QDataStream stream(&locationState, QIODevice::ReadOnly);
        m_view->restoreState(stream);
    }
}

QString DolphinViewContainer::getNearestExistingAncestorOfPath(const QString &path) const
{
    QDir dir(path);
    do {
        dir.setPath(QDir::cleanPath(dir.filePath(QStringLiteral(".."))));
    } while (!dir.exists() && !dir.isRoot());

    return dir.exists() ? dir.path() : QString{};
}

void DolphinViewContainer::updateStatusBarGeometry()
{
    if (!m_statusBar) {
        return;
    }
    if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::Small) {
        QRect statusBarRect(preferredSmallStatusBarGeometry());
        if (view()->layoutDirection() == Qt::RightToLeft) {
            const int splitterWidth = m_statusBar->clippingAmount();
            statusBarRect.setLeft(rect().width() - m_statusBar->width() + splitterWidth); // Add clipping amount.
        }
        // Move statusbar to bottomLeft, or bottomRight with right-to-left-layout.
        m_statusBar->setGeometry(statusBarRect);
        // Add 1 due to how qrect coordinates work.
        m_view->setStatusBarOffset(m_statusBar->geometry().height() - m_statusBar->clippingAmount() + 1);
    } else {
        m_view->setStatusBarOffset(0);
    }
}

bool DolphinViewContainer::eventFilter(QObject *object, QEvent *event)
{
    if (GeneralSettings::showStatusBar() == GeneralSettings::EnumShowStatusBar::Small && object == m_view) {
        switch (event->type()) {
        case QEvent::Resize: {
            m_statusBar->updateWidthToContent();
            break;
        }
        case QEvent::LayoutRequest: {
            m_statusBar->updateWidthToContent();
            break;
        }
        default:
            break;
        }
    }
    return false;
}

QRect DolphinViewContainer::preferredSmallStatusBarGeometry()
{
    // Add offset depending if horizontal scrollbar or filterbar is visible, we need to add 1 due to how QRect coordinates work.
    const int yPos = m_view->geometry().bottom() - m_view->horizontalScrollBarHeight() - m_statusBar->minimumHeight() + 1;
    QRect statusBarRect = rect().adjusted(0, yPos, 0, 0);
    return statusBarRect;
}

#include "moc_dolphinviewcontainer.cpp"

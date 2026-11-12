/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 *
 * Based on the Itemviews NG project from Trolltech Labs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kitemlistwidget.h"

#include "kitemlistview.h"
#include "private/kitemlistselectiontoggle.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QDateTime>
#include <QHash>

#include <QApplication>
#include <QSet>
#include <QLinearGradient>
#include <KApplicationTrader>
#include <KService>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QCoreApplication>
#include <QStandardPaths>
#include <signal.h>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QStyleOption>

KItemListWidgetInformant::KItemListWidgetInformant() = default;

KItemListWidgetInformant::~KItemListWidgetInformant() = default;

KItemListWidget::KItemListWidget(KItemListWidgetInformant *informant, QGraphicsItem *parent)
    : QGraphicsWidget(parent)
    , m_informant(informant)
    , m_index(-1)
    , m_selected(false)
    , m_current(false)
    , m_hovered(false)
    , m_expansionAreaHovered(false)
    , m_alternateBackground(false)
    , m_enabledSelectionToggle(false)
    , m_clickHighlighted(false)
    , m_data()
    , m_visibleRoles()
    , m_columnWidths()
    , m_leftPadding(0)
    , m_rightPadding(0)
    , m_styleOption()
    , m_siblingsInfo()
    , m_hoverOpacity(0)
    , m_hoverCache(nullptr)
    , m_hoverSequenceIndex(0)
    , m_selectionToggle(nullptr)
    , m_editedRole()
    , m_iconSize(-1)
{
    connect(&m_hoverSequenceTimer, &QTimer::timeout, this, &KItemListWidget::slotHoverSequenceTimerTimeout);
}

KItemListWidget::~KItemListWidget()
{
    clearHoverCache();
}

void KItemListWidget::setIndex(int index)
{
    if (m_index != index) {
        delete m_selectionToggle;
        m_selectionToggle = nullptr;

        m_hoverOpacity = 0;

        clearHoverCache();

        m_index = index;
    }
}

int KItemListWidget::index() const
{
    return m_index;
}

void KItemListWidget::setData(const QHash<QByteArray, QVariant> &data, const QSet<QByteArray> &roles)
{
    clearHoverCache();
    if (roles.isEmpty()) {
        m_data = data;
        dataChanged(m_data);
    } else {
        for (const QByteArray &role : roles) {
            m_data[role] = data[role];
        }
        dataChanged(m_data, roles);
    }
    update();
}

QHash<QByteArray, QVariant> KItemListWidget::data() const
{
    return m_data;
}

QVariant KItemListWidget::value(const QByteArray &key) const
{
    return m_data.value(key);
}

namespace
{
struct NgCached {
    bool present = false;
    QString value;
};
QHash<QByteArray, NgCached> &ngCache()
{
    static QHash<QByteArray, NgCached> cache;
    return cache;
}
int ngGeneration = 0;
}

NgSettings *NgSettings::instance()
{
    static NgSettings settings;
    return &settings;
}

void ngSettingsInvalidate()
{
    ngCache().clear();
    ++ngGeneration;
    Q_EMIT NgSettings::instance()->changed();
}

int ngSettingsGeneration()
{
    return ngGeneration;
}

QString ngTr(const char *english, const char *italian)
{
    static const bool useItalian = KLocalizedString::languages().value(0).startsWith(QLatin1String("it"));
    return QString::fromUtf8(useItalian ? italian : english);
}

// Values are cached: the row painting asks for them many times per frame. ngSettingsInvalidate() clears the cache.
QString ngSetting(const char *name, const QString &fallback)
{
    const QByteArray key(name);
    auto it = ngCache().constFind(key);
    if (it == ngCache().constEnd()) {
        NgCached entry;
        const QByteArray variable = QByteArray("DOLPHIN_NG_") + name;
        if (qEnvironmentVariableIsSet(variable.constData())) {
            entry.present = true;
            entry.value = qEnvironmentVariable(variable.constData());
        } else {
            const KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
            entry.present = group.hasKey(name);
            entry.value = group.readEntry(name, QString());
        }
        it = ngCache().insert(key, entry);
    }
    return it->present ? it->value : fallback;
}

int ngSettingInt(const char *name, int fallback)
{
    bool ok = false;
    const int value = ngSetting(name).toInt(&ok);
    return ok ? value : fallback;
}

void setNgSetting(const char *name, const QString &value)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
    if (value.isEmpty()) {
        group.deleteEntry(name);
    } else {
        group.writeEntry(name, value);
    }
    group.sync();
    ngCache().remove(QByteArray(name));
}

bool ngSettingFromEnvironment(const char *name)
{
    const QByteArray variable = QByteArray("DOLPHIN_NG_") + name;
    return qEnvironmentVariableIsSet(variable.constData());
}

NgCardStyle ngCardStyle()
{
    const QString look = ngSetting("CARDS").toLower();
    if (look == QLatin1String("b") || look == QLatin1String("scheda")) {
        return NgCardStyle::Scheda;
    }
    if (look == QLatin1String("c") || look == QLatin1String("dossier")) {
        return NgCardStyle::Dossier;
    }
    return NgCardStyle::Tessera;
}

bool ngCardsStretch()
{
    return ngSetting("CARDS_FIT").toLower() == QLatin1String("stretch");
}

static QList<NgQuickAction> ngQuickActionsCompute(const QString &localPath, const QString &mime);

NgActivity *NgActivity::self()
{
    static NgActivity *instance = new NgActivity();
    return instance;
}

NgActivity::NgActivity()
{
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, &NgActivity::tick);
    m_sweep.setInterval(1000);
    connect(&m_sweep, &QTimer::timeout, this, &NgActivity::sweepFamilies);
    QTimer::singleShot(0, this, &NgActivity::loadProcesses);
}

static QString ngActivityStateFile(qint64 owner)
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/dolphin-ng-activity-%1").arg(owner);
}

void NgActivity::saveProcesses()
{
    const QString path = ngActivityStateFile(QCoreApplication::applicationPid());
    if (m_pids.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    // pid, url, label, progress file, whether its results are tied to it: one line each, tab separated
    for (auto it = m_pids.cbegin(); it != m_pids.cend(); ++it) {
        if (m_links.contains(it.key())) {
            continue; // a child waiting with its parent's pid: it comes back with the parent
        }
        const QStringList fields{QString::number(it.value()), it.key(), m_entries.value(it.key()).text, m_progressFiles.value(it.key()),
                                 m_watched.contains(it.key()) ? QStringLiteral("1") : QStringLiteral("0")};
        file.write(fields.join(QLatin1Char('\t')).toUtf8() + '\n');
    }
}

void NgActivity::loadProcesses()
{
    // work started by a Dolphin that was closed (or by another window process) and still running
    const QDir dir(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
    const QStringList files = dir.entryList({QStringLiteral("dolphin-ng-activity-*")}, QDir::Files);
    for (const QString &name : files) {
        const qint64 owner = name.section(QLatin1Char('-'), -1).toLongLong();
        if (owner == QCoreApplication::applicationPid()) {
            continue;
        }
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        file.close();
        if (owner <= 0 || ::kill(static_cast<pid_t>(owner), 0) != 0) {
            QFile::remove(file.fileName()); // its Dolphin is gone: this one takes the list over
        }
        for (const QString &line : lines) {
            const QStringList f = line.split(QLatin1Char('\t'));
            const qint64 pid = f.value(0).toLongLong();
            if (f.size() < 5 || pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) != 0) {
                continue;
            }
            const QUrl url(f.at(1));
            setUntilProcessEnds(url, pid, f.at(2), f.at(3));
            if (f.at(4) == QLatin1String("1")) {
                watchForChildren(url);
            }
        }
    }
}

QString NgActivity::key(const QUrl &url)
{
    return url.adjusted(QUrl::StripTrailingSlash | QUrl::NormalizePathSegments).toString();
}

void NgActivity::set(const QUrl &url, int percent, const QString &text)
{
    if (!url.isValid()) {
        return;
    }
    Entry &e = m_entries[key(url)];
    if (!e.startedAt) {
        e.startedAt = QDateTime::currentMSecsSinceEpoch();
    }
    if (e.percent == percent && e.text == text) {
        return;
    }
    e.percent = qBound(-1, percent, 100);
    e.text = text;
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    Q_EMIT changed();
}

void NgActivity::clear(const QUrl &url)
{
    const QString k = key(url);
    m_cancellers.remove(k);
    if (m_pids.remove(k)) {
        saveProcesses();
    }
    if (m_entries.remove(k)) {
        parentDone(k);
        Q_EMIT changed();
    }
}

void NgActivity::watchForChildren(const QUrl &parent)
{
    if (parent.isValid()) {
        m_watched.insert(key(parent), parent);
    }
}

void NgActivity::offerNewFile(const QUrl &url)
{
    if (m_watched.isEmpty()) {
        return;
    }
    const QUrl folder = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    const QString name = url.adjusted(QUrl::StripTrailingSlash).fileName();
    for (const QUrl &parent : std::as_const(m_watched)) {
        if (parent.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash) != folder || key(parent) == key(url)) {
            continue;
        }
        // named after the parent: "Holiday.zip" gives "Holiday", "talk.ogg" gives "talk.txt", "doc.pdf" gives "doc-1.png"
        const QString stem = QFileInfo(parent.fileName()).baseName();
        if (stem.isEmpty() || !name.startsWith(stem)) {
            continue;
        }
        const QChar next = name.size() > stem.size() ? name.at(stem.size()) : QChar();
        if (next.isNull() || next == QLatin1Char('.') || next == QLatin1Char('-') || next == QLatin1Char('_') || next == QLatin1Char(' ') || next == QLatin1Char('(')) {
            link(parent, url);
            return;
        }
    }
}

void NgActivity::link(const QUrl &parent, const QUrl &child)
{
    const QString childKey = key(child), parentKey = key(parent);
    if (childKey == parentKey || m_links.contains(childKey) || m_links.contains(parentKey)) {
        return; // one level only: a child does not get children of its own
    }
    m_links.insert(childKey, {child, parent});
    m_linkedAt.insert(childKey, QDateTime::currentMSecsSinceEpoch());
    if (!m_sweep.isActive()) {
        m_sweep.start();
    }
    if (!m_timer.isActive()) {
        m_timer.start(); // the new file grows in
    }
    // while the parent is still at work, the new file waits with a turning arc
    if (m_entries.contains(parentKey) && !m_entries.contains(childKey)) {
        const QString waiting = ngTr("On its way…", "In arrivo…");
        if (m_pids.contains(parentKey)) {
            setUntilProcessEnds(child, m_pids.value(parentKey), waiting);
        } else {
            set(child, -1, waiting);
        }
    }
    Q_EMIT linksChanged();
    Q_EMIT changed();
}

void NgActivity::parentDone(const QString &parentKey)
{
    const bool isParent = m_watched.contains(parentKey) || std::any_of(m_links.cbegin(), m_links.cend(), [&](const auto &l) {
        return key(l.second) == parentKey;
    });
    if (!isParent) {
        return;
    }
    // files written at the very end show up a moment later: keep looking for them a few seconds
    QTimer::singleShot(5000, this, [this, parentKey] {
        if (!m_entries.contains(parentKey)) {
            m_watched.remove(parentKey);
        }
    });
    // the end of the tie itself is left to sweepFamilies(), which looks at every family once a second
}

void NgActivity::sweepFamilies()
{
    // a family whose files are all idle (the parent and its children no longer at work) keeps its bubble 30 s,
    // so the result is easy to find; then the bubble fades out and the files go back in order. Checked every
    // second, whatever way the family was made or its work ended
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> parents;
    for (const auto &l : std::as_const(m_links)) {
        parents.insert(key(l.second));
    }
    bool removed = false;
    for (const QString &parentKey : std::as_const(parents)) {
        bool busy = m_entries.contains(parentKey) || m_watched.contains(parentKey);
        for (auto it = m_links.cbegin(); it != m_links.cend() && !busy; ++it) {
            busy = key(it->second) == parentKey && m_pids.contains(it.key());
        }
        if (busy) {
            m_idleSince.remove(parentKey);
            m_fadingSince.remove(parentKey);
            continue;
        }
        const qint64 idle = m_idleSince.value(parentKey, 0);
        if (!idle) {
            m_idleSince.insert(parentKey, now);
        } else if (!m_fadingSince.contains(parentKey) && now - idle >= 30000) {
            m_fadingSince.insert(parentKey, now);
            if (!m_timer.isActive()) {
                m_timer.start(); // repaints while it fades
            }
        } else if (m_fadingSince.contains(parentKey) && now - m_fadingSince.value(parentKey) >= 520) {
            m_fadingSince.remove(parentKey);
            m_idleSince.remove(parentKey);
            for (auto it = m_links.begin(); it != m_links.end();) {
                if (key(it->second) == parentKey) {
                    m_entries.remove(it.key());
                    m_linkedAt.remove(it.key());
                    it = m_links.erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
        }
    }
    if (m_links.isEmpty()) {
        m_sweep.stop();
        m_idleSince.clear();
        m_fadingSince.clear();
    }
    if (removed) {
        Q_EMIT linksChanged();
        Q_EMIT changed();
    }
}

qreal NgActivity::fading(const QUrl &url) const
{
    if (m_fadingSince.isEmpty()) {
        return 1;
    }
    const QString family = familyOf(url);
    const qint64 since = m_fadingSince.value(family, 0);
    return since ? qBound<qreal>(0, 1 - (QDateTime::currentMSecsSinceEpoch() - since) / 500.0, 1) : 1;
}

QUrl NgActivity::parentOf(const QUrl &child) const
{
    return m_links.isEmpty() ? QUrl() : m_links.value(key(child)).second;
}

QString NgActivity::familyOf(const QUrl &url) const
{
    if (m_links.isEmpty()) {
        return QString();
    }
    const QString k = key(url);
    const auto it = m_links.constFind(k);
    if (it != m_links.cend()) {
        return key(it->second);
    }
    for (const auto &l : m_links) {
        if (key(l.second) == k) {
            return k;
        }
    }
    return QString();
}

QList<std::pair<QUrl, QUrl>> NgActivity::links() const
{
    return m_links.values();
}

bool NgActivity::hasLinks() const
{
    return !m_links.isEmpty();
}

bool NgActivity::contains(const QUrl &url) const
{
    return !m_entries.isEmpty() && m_entries.contains(key(url));
}

NgActivity::Entry NgActivity::entry(const QUrl &url) const
{
    return m_entries.value(key(url));
}

bool NgActivity::isEmpty() const
{
    return m_entries.isEmpty();
}

void NgActivity::setUntilProcessEnds(const QUrl &url, qint64 pid, const QString &text, const QString &progressFile)
{
    set(url, -1, text);
    m_pids.insert(key(url), pid);
    if (!progressFile.isEmpty()) {
        m_progressFiles.insert(key(url), progressFile);
    }
    saveProcesses();
}

void NgActivity::setFamilyColour(const QString &family, const QColor &colour)
{
    if (m_familyColours.value(family) != colour) {
        m_familyColours.insert(family, colour);
        Q_EMIT changed(); // the other pieces of the bubble take it
    }
}

QColor NgActivity::familyColour(const QString &family) const
{
    return m_familyColours.value(family);
}

void NgActivity::setHoveredStop(const QUrl &url)
{
    const QString k = url.isValid() ? key(url) : QString();
    if (k != m_hoveredStop) {
        m_hoveredStop = k;
        Q_EMIT changed();
    }
}

bool NgActivity::isStopHovered(const QUrl &url) const
{
    return !m_hoveredStop.isEmpty() && m_hoveredStop == key(url);
}

bool NgActivity::canCancel(const QUrl &url) const
{
    const QString k = key(url);
    return m_entries.contains(k) && !m_links.contains(k) && (m_pids.contains(k) || m_cancellers.contains(k));
}

void NgActivity::setCanceller(const QUrl &url, const std::function<void()> &cancel)
{
    m_cancellers.insert(key(url), cancel);
}

void NgActivity::cancel(const QUrl &url)
{
    const QString k = key(url);
    if (m_pids.contains(k)) {
        // a quick action runs in its own session (detached): stop the whole group, the programs it started too
        const pid_t pid = static_cast<pid_t>(m_pids.value(k));
        ::kill(-pid, SIGTERM);
        ::kill(pid, SIGTERM);
    } else if (m_cancellers.contains(k)) {
        const auto cancelIt = m_cancellers.take(k);
        cancelIt();
    } else {
        return;
    }
    // what it was making is not coming: untie it now
    bool untied = false;
    for (auto it = m_links.begin(); it != m_links.end();) {
        if (key(it->second) == k) {
            m_entries.remove(it.key());
            m_pids.remove(it.key());
            it = m_links.erase(it);
            untied = true;
        } else {
            ++it;
        }
    }
    m_watched.remove(k);
    if (m_entries.contains(k)) {
        m_entries[k].text = ngTr("Stopping…", "Annullamento…");
        m_entries[k].percent = -1;
    }
    if (untied) {
        Q_EMIT linksChanged();
    }
    Q_EMIT changed();
}

qreal NgActivity::growth(const QUrl &url) const
{
    if (m_linkedAt.isEmpty()) {
        return 1;
    }
    const QString k = key(url);
    qint64 at = m_linkedAt.value(k, 0);
    if (!at) {
        for (auto it = m_links.cbegin(); it != m_links.cend(); ++it) {
            if (key(it->second) == k) {
                at = qMax(at, m_linkedAt.value(it.key(), 0));
            }
        }
    }
    return at ? qBound<qreal>(0, (QDateTime::currentMSecsSinceEpoch() - at) / 420.0, 1) : 1;
}

bool NgActivity::growing() const
{
    // (also while a bubble fades out: the same repaints)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return !m_fadingSince.isEmpty() || std::any_of(m_linkedAt.cbegin(), m_linkedAt.cend(), [now](qint64 at) {
        return now - at < 600;
    });
}

qreal NgActivity::phase() const
{
    return m_phase;
}

void NgActivity::tick()
{
    // processes started for quick actions: done when they are gone (they are detached: no zombie stays behind)
    static int slow = 0;
    bool saveLater = false;
    if (++slow >= 8) {
        slow = 0;
        for (auto it = m_pids.begin(); it != m_pids.end();) {
            if (::kill(static_cast<pid_t>(it.value()), 0) != 0) {
                m_entries.remove(it.key());
                m_cancellers.remove(it.key());
                parentDone(it.key());
                saveLater = true;
                if (m_progressFiles.contains(it.key())) {
                    QFile::remove(m_progressFiles.take(it.key()));
                }
                it = m_pids.erase(it);
            } else {
                // how far it is, if the program says so
                const QString progressFile = m_progressFiles.value(it.key());
                QFile file(progressFile);
                if (!progressFile.isEmpty() && file.open(QIODevice::ReadOnly)) {
                    bool ok = false;
                    const int percent = QString::fromLatin1(file.read(16)).trimmed().toInt(&ok);
                    auto entry = m_entries.find(it.key());
                    if (ok && percent >= 0 && percent <= 100 && entry != m_entries.end() && entry->percent != percent) {
                        entry->percent = percent;
                        Q_EMIT changed();
                    }
                }
                ++it;
            }
        }
    }
    if (saveLater) {
        saveProcesses();
    }
    const bool grows = growing();
    if (grows) {
        Q_EMIT changed();
    }
    if (m_entries.isEmpty()) {
        if (!grows) {
            m_timer.stop();
            Q_EMIT changed(); // one last repaint, to take the marks away
        }
        return;
    }
    bool turning = false;
    for (const Entry &e : std::as_const(m_entries)) {
        turning = turning || e.percent < 0;
    }
    if (turning) {
        m_phase = std::fmod(m_phase + 0.03, 1.0);
        Q_EMIT changed();
    }
}

QList<NgQuickAction> ngQuickActionsFor(const QString &localPath, const QString &mime)
{
    static QHash<QString, QList<NgQuickAction>> cache;
    const QString key = localPath + QLatin1Char('|') + mime;
    auto it = cache.constFind(key);
    if (it == cache.constEnd()) {
        if (cache.size() > 4000) {
            cache.clear();
        }
        it = cache.insert(key, ngQuickActionsCompute(localPath, mime));
    }
    return *it;
}

// ng-transcribe (ng/ng-transcribe), installed as a command: in PATH or in ~/.local/bin, which a session started
// from a menu may not have in PATH
static QString ngTranscriber()
{
    static const QString path = [] {
        QString p = QStandardPaths::findExecutable(QStringLiteral("ng-transcribe"));
        const QString local = QDir::homePath() + QStringLiteral("/.local/bin/ng-transcribe");
        if (p.isEmpty() && QFileInfo(local).isExecutable()) {
            p = local;
        }
        return p;
    }();
    return path;
}

static QList<NgQuickAction> ngQuickActionsCompute(const QString &localPath, const QString &mime)
{
    QList<NgQuickAction> actions;
    if (localPath.isEmpty() || mime.isEmpty()) {
        return actions;
    }
    if (mime == QLatin1String("inode/directory")) {
        // a folder: only cut and copy (a click opens it, there is nothing to convert or extract)
        NgQuickAction cut;
        cut.label = ngTr("Cut", "Taglia");
        cut.program = QStringLiteral("@cut");
        cut.icon = QStringLiteral("edit-cut");
        NgQuickAction copy;
        copy.label = ngTr("Copy", "Copia");
        copy.program = QStringLiteral("@copy");
        copy.icon = QStringLiteral("edit-copy");
        return {cut, copy};
    }
    const QFileInfo info(localPath);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();
    const QString lowerSuffix = suffix.toLower();
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
    auto simple = [&actions](const QString &label, const QString &program, const QStringList &arguments) {
        NgQuickAction action;
        action.label = label;
        action.program = program;
        action.arguments = arguments;
        actions.append(action);
    };
    if (isImage) {
        NgQuickAction resize;
        resize.label = ngTr("Resize", "Ridimensiona");
        resize.program = QStringLiteral("magick");
        for (const auto &[label, spec, tag] : {std::make_tuple(QStringLiteral("75 %"), QStringLiteral("75%"), QStringLiteral("75")),
                                               std::make_tuple(QStringLiteral("50 %"), QStringLiteral("50%"), QStringLiteral("50")),
                                               std::make_tuple(QStringLiteral("25 %"), QStringLiteral("25%"), QStringLiteral("25")),
                                               std::make_tuple(ngTr("Width 1920", "Larghezza 1920"), QStringLiteral("1920x>"), QStringLiteral("1920")),
                                               std::make_tuple(ngTr("Width 1280", "Larghezza 1280"), QStringLiteral("1280x>"), QStringLiteral("1280")),
                                               std::make_tuple(ngTr("Width 800", "Larghezza 800"), QStringLiteral("800x>"), QStringLiteral("800"))}) {
            resize.choices.append({label, {localPath, QStringLiteral("-resize"), spec, QStringLiteral("%1-%2.%3").arg(base, tag, suffix)}});
        }
        actions.append(resize);
        if (mime == QLatin1String("image/gif")) {
            simple(ngTr("To MP4", "In MP4"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-movflags"), QStringLiteral("faststart"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-vf"), QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2"), QStringLiteral("%1.mp4").arg(base)});
        }
    } else if (archives.contains(mime)) {
        static const bool hasArk = !QStandardPaths::findExecutable(QStringLiteral("ark")).isEmpty();
        if (hasArk) {
            simple(ngTr("Extract here", "Estrai qui"), QStringLiteral("ark"), {QStringLiteral("--batch"), QStringLiteral("--autosubfolder"), localPath});
        } else {
            simple(ngTr("Extract here", "Estrai qui"), QStringLiteral("bsdtar"), {QStringLiteral("-xf"), localPath, QStringLiteral("--one-top-level=%1").arg(base)});
        }
    } else if (mime == QLatin1String("application/pdf")) {
        simple(ngTr("To PNG", "In PNG"), QStringLiteral("pdftoppm"), {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("150"), localPath, base});
        simple(ngTr("Extract the text", "Estrai il testo"), QStringLiteral("pdftotext"), {QStringLiteral("-layout"), localPath, QStringLiteral("%1.txt").arg(base)});
    } else if (mime.startsWith(QLatin1String("audio/"))) {
        // speech to text with ng-transcribe (whisper.cpp, on this computer), when it is installed: the main action
        if (!ngTranscriber().isEmpty()) {
            simple(ngTr("Transcribe", "Trascrivi"), ngTranscriber(), {localPath});
        }
        if (lowerSuffix != QLatin1String("mp3")) {
            simple(ngTr("To MP3", "In MP3"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
        }
        if (lowerSuffix != QLatin1String("flac")) {
            simple(ngTr("To FLAC", "In FLAC"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("%1.flac").arg(base)});
        }
    } else if (mime.startsWith(QLatin1String("video/"))) {
        simple(ngTr("To MP4", "In MP4"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-crf"), QStringLiteral("23"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("%1.mp4").arg(base)});
        simple(ngTr("Extract the audio (MP3)", "Estrai l'audio (MP3)"), QStringLiteral("ffmpeg"), {QStringLiteral("-i"), localPath, QStringLiteral("-vn"), QStringLiteral("-q:a"), QStringLiteral("2"), QStringLiteral("%1.mp3").arg(base)});
        if (!ngTranscriber().isEmpty()) {
            simple(ngTr("Transcribe", "Trascrivi"), ngTranscriber(), {localPath});
        }
        simple(ngTr("Frame to PNG", "Fotogramma in PNG"), QStringLiteral("ffmpeg"), {QStringLiteral("-ss"), QStringLiteral("1"), QStringLiteral("-i"), localPath, QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("%1-1s.png").arg(base)});
    }
    // icons; the first action of the kind is the main one
    static const QHash<QString, QString> icons{{ngTr("Resize", "Ridimensiona"), QStringLiteral("view-fullscreen")},
                                               {ngTr("To MP4", "In MP4"), QStringLiteral("video-x-generic")},
                                               {ngTr("Extract here", "Estrai qui"), QStringLiteral("folder-download")},
                                               {ngTr("To PNG", "In PNG"), QStringLiteral("image-x-generic")},
                                               {ngTr("Extract the text", "Estrai il testo"), QStringLiteral("text-plain")},
                                               {ngTr("To MP3", "In MP3"), QStringLiteral("audio-x-generic")},
                                               {ngTr("To FLAC", "In FLAC"), QStringLiteral("audio-x-generic")},
                                               {ngTr("Extract the audio (MP3)", "Estrai l'audio (MP3)"), QStringLiteral("audio-x-generic")},
                                               {ngTr("Frame to PNG", "Fotogramma in PNG"), QStringLiteral("camera-photo")},
                                               {ngTr("Transcribe", "Trascrivi"), QStringLiteral("text-x-generic")}};
    for (NgQuickAction &action : actions) {
        action.icon = icons.value(action.label, QStringLiteral("system-run"));
    }
    if (!actions.isEmpty()) {
        actions[0].main = true;
    }
    // then the application that opens this type after the default one (a click on the card already opens the
    // default), "open with", and cut / copy (the runner,
    // DolphinView, launches them: "@app" + storage id, "@openwith" = the whole menu). Asked once per type.
    static QHash<QString, QList<NgQuickAction>> appsForMime;
    auto apps = appsForMime.constFind(mime);
    if (apps == appsForMime.constEnd()) {
        QList<NgQuickAction> list;
        const KService::List services = KApplicationTrader::queryByMimeType(mime);
        QSet<QString> seenIcons; // two launchers of the same program (same icon) would read as a duplicate
        if (!services.isEmpty()) {
            seenIcons.insert(services.constFirst()->icon());
        }
        for (int n = 1; n < services.count() && list.count() < 1; ++n) {
            if (seenIcons.contains(services.at(n)->icon())) {
                continue;
            }
            seenIcons.insert(services.at(n)->icon());
            NgQuickAction action;
            action.label = ngTr("Open with %1", "Apri con %1").arg(services.at(n)->name());
            action.program = QStringLiteral("@app");
            action.arguments = {services.at(n)->storageId()};
            action.icon = services.at(n)->icon();
            list.append(action);
        }
        NgQuickAction openWith;
        openWith.label = ngTr("Open with…", "Apri con…");
        openWith.program = QStringLiteral("@openwith");
        openWith.icon = QStringLiteral("overflow-menu");
        list.append(openWith);
        NgQuickAction cut;
        cut.label = ngTr("Cut", "Taglia");
        cut.program = QStringLiteral("@cut");
        cut.icon = QStringLiteral("edit-cut");
        list.append(cut);
        NgQuickAction copy;
        copy.label = ngTr("Copy", "Copia");
        copy.program = QStringLiteral("@copy");
        copy.icon = QStringLiteral("edit-copy");
        list.append(copy);
        apps = appsForMime.insert(mime, list);
    }
    int at = actions.isEmpty() ? 0 : 1;
    for (const NgQuickAction &action : *apps) {
        actions.insert(at++, action);
    }
    return actions;
}

namespace
{
NgRowStyle ngRowStyleFromName(const QByteArray &name, NgRowStyle fallback)
{
    return name == "stripe" ? NgRowStyle::Stripe
        : name == "line"    ? NgRowStyle::Line
        : name == "none"    ? NgRowStyle::None
        : name == "open"    ? NgRowStyle::Open
        : name == "card"    ? NgRowStyle::Card
                            : fallback;
}
// the current style: DOLPHIN_NG_ROWS if set, else what was last chosen in the window, else cards
NgRowStyle &ngRowStyleRef()
{
    static NgRowStyle style = NgRowStyle::Card;
    static int generation = -1;
    if (generation != ngSettingsGeneration()) {
        generation = ngSettingsGeneration();
        const QByteArray saved = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG")).readEntry("RowStyle", QByteArray("card")).toLower();
        style = ngRowStyleFromName(qgetenv("DOLPHIN_NG_ROWS").toLower(), ngRowStyleFromName(saved, NgRowStyle::Card));
    }
    return style;
}
}

NgRowStyle ngRowStyle()
{
    return ngRowStyleRef();
}

void setNgRowStyle(NgRowStyle style)
{
    ngRowStyleRef() = style;
    static const char *const names[] = {"stripe", "card", "line", "none", "open"};
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("DolphinNG"));
    group.writeEntry("RowStyle", QByteArray(names[int(style)]));
    group.sync();
}

bool ngTwoLineList()
{
    const bool columns = ngSetting("LIST").toLower() == QLatin1String("columns");
    return !columns && QGuiApplication::layoutDirection() == Qt::LeftToRight;
}

QColor ngCategoryTint(NgKind::Category category)
{
    static const QColor tints[NgKind::CategoryCount] = {QColor(201, 205, 213), QColor(195, 164, 230), QColor(143, 188, 230), QColor(127, 214, 168), QColor(230, 183, 122),
                                                        QColor(127, 208, 193), QColor(230, 164, 200), QColor(240, 163, 154), QColor(176, 181, 191)};
    return tints[category];
}

NgKind ngKindFor(const QString &name, bool isDir)
{
    auto kind = [](const QString &sigla, NgKind::Category category) {
        // a few types have a colour everybody knows them by, whatever their category: PDF red, slides orange, RPM crimson
        static const QHash<QString, QColor> own{{QStringLiteral("PDF"), QColor(238, 108, 98)},
                                                {QStringLiteral("PPT"), QColor(238, 150, 104)},
                                                {QStringLiteral("PPTX"), QColor(238, 150, 104)},
                                                {QStringLiteral("ODP"), QColor(238, 150, 104)},
                                                {QStringLiteral("RPM"), QColor(214, 72, 104)}}; // a crimson: red, but not the PDF red
        const auto colour = own.constFind(sigla);
        return NgKind{sigla, colour != own.constEnd() ? *colour : ngCategoryTint(category), category};
    };
    if (isDir) {
        return kind(QStringLiteral("DIR"), NgKind::Folder);
    }
    const QString lower = name.toLower();
    static const QList<std::pair<QString, QString>> compound{{QStringLiteral(".tar.gz"), QStringLiteral("TGZ")},
                                                             {QStringLiteral(".tar.xz"), QStringLiteral("TXZ")},
                                                             {QStringLiteral(".tar.bz2"), QStringLiteral("TBZ")},
                                                             {QStringLiteral(".tar.zst"), QStringLiteral("TZST")}};
    for (const auto &entry : compound) {
        if (lower.endsWith(entry.first)) {
            return kind(entry.second, NgKind::Archive);
        }
    }
    const int dot = lower.lastIndexOf(QLatin1Char('.'));
    const QString ext = (dot > 0 && dot < lower.size() - 1) ? lower.mid(dot + 1) : QString();
    if (ext.isEmpty() || ext.size() > 5) {
        return kind(QStringLiteral("FILE"), NgKind::Other);
    }
    static const QSet<QString> images{"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "tif", "tiff", "heic", "avif", "ico", "xcf", "kra"};
    static const QSet<QString> documents{"pdf", "doc", "docx", "odt", "rtf", "txt", "md", "epub", "ppt", "pptx", "odp", "msg", "eml"};
    static const QSet<QString> sheets{"xls", "xlsx", "ods", "csv", "tsv"};
    static const QSet<QString> archives{"zip", "gz", "tgz", "xz", "bz2", "7z", "rar", "tar", "zst"};
    static const QSet<QString> medias{"mp3", "flac", "ogg", "wav", "m4a", "opus", "mp4", "mkv", "webm", "avi", "mov"};
    static const QSet<QString> datas{"json", "xml", "yaml", "yml", "toml", "ini", "conf", "html", "css", "js", "ts", "py", "rs", "c", "cpp", "h", "sh", "sql", "log"};
    static const QSet<QString> packages{"rpm", "deb", "exe", "msi", "iso", "img", "apk", "jar"};
    const NgKind::Category category = images.contains(ext) ? NgKind::Image
        : documents.contains(ext)                           ? NgKind::Document
        : sheets.contains(ext)                              ? NgKind::Sheet
        : archives.contains(ext)                            ? NgKind::Archive
        : medias.contains(ext)                              ? NgKind::Media
        : datas.contains(ext)                               ? NgKind::Data
        : packages.contains(ext)                            ? NgKind::Package
                                                            : NgKind::Other;
    return kind(ext.toUpper(), category);
}

// dolphin-ng: "oggi", "ieri", "7 giorni fa", "2 anni fa"
QString ngAgoText(const QDateTime &when)
{
    const qint64 days = when.date().daysTo(QDate::currentDate());
    if (days <= 0) {
        return ngTr("today", "oggi");
    }
    if (days == 1) {
        return ngTr("yesterday", "ieri");
    }
    if (days < 365) {
        return ngTr("%1 days ago", "%1 giorni fa").arg(days);
    }
    const qint64 years = days / 365;
    return years == 1 ? ngTr("1 year ago", "1 anno fa") : ngTr("%1 years ago", "%1 anni fa").arg(years);
}

void KItemListWidget::ngSettingsChanged()
{
    update();
}

int KItemListWidget::ngActionAt(const QPointF &pos) const
{
    Q_UNUSED(pos)
    return 0;
}

bool ngTiles()
{
    const bool off = ngSetting("TILES").toLower() == QLatin1String("off");
    return !off;
}

bool ngIsDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightnessF() < 0.5;
}

QColor ngAccent(const QColor &colour, bool dark)
{
    float h, s, l;
    colour.getHslF(&h, &s, &l);
    return QColor::fromHslF(h < 0 ? 0 : h, dark ? s : qMin(1.0f, s * 1.15f + 0.05f), dark ? qMax(l, 0.64f) : qMin(l, 0.40f));
}

bool ngNoIcons()
{
    const bool show = ngSetting("ICONS").toLower() == QLatin1String("show");
    return !show && ngTwoLineList();
}

void KItemListWidget::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)

    // dolphin-ng: with any style but "stripe" the view sets m_alternateBackground on every row
    // (see KItemListView::updateAlternateBackgroundForWidget) and the row is drawn here.
    if (m_alternateBackground && ngRowStyle() == NgRowStyle::Card) {
        // a rounded card per row, darker than the view, with a hairline of light along its top edge;
        // same footprint as the hover/selection shape of the style
        const QRectF card = selectionRectFull().adjusted(3, 3, -3, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        const bool dark = ngIsDark(m_styleOption.palette);
        painter->setBrush(dark ? QColor(0, 0, 0, 66) : QColor(0, 0, 0, 14)); // darker than the view on either scheme
        painter->drawRoundedRect(card, 5, 5);
        painter->setPen(QPen(dark ? QColor(255, 255, 255, 11) : QColor(255, 255, 255, 170), 1));
        painter->drawLine(QPointF(card.left() + 5, card.top() + 0.5), QPointF(card.right() - 5, card.top() + 0.5));
        painter->restore();
    } else if (m_alternateBackground && ngRowStyle() == NgRowStyle::Open) {
        // open rows: no card and no separator; the row is told apart by its colour wash alone
    } else if (m_alternateBackground && ngRowStyle() == NgRowStyle::Line) {
        // a hairline under the row that fades out towards both ends
        QColor lineColor = m_styleOption.palette.color(QPalette::Text);
        QColor transparent = lineColor;
        lineColor.setAlphaF(0.10);
        transparent.setAlphaF(0.0);
        QLinearGradient gradient(0, 0, size().width(), 0);
        gradient.setColorAt(0.0, transparent);
        gradient.setColorAt(0.12, lineColor);
        gradient.setColorAt(0.88, lineColor);
        gradient.setColorAt(1.0, transparent);
        painter->fillRect(QRectF(0, size().height() - 1, size().width(), 1), gradient);
    } else if (m_alternateBackground && ngRowStyle() == NgRowStyle::Stripe) {
        QColor backgroundColor = m_styleOption.palette.color(QPalette::AlternateBase);
        if (!widget->hasFocus()) {
            QColor baseColor = m_styleOption.palette.color(QPalette::Base);
            if (baseColor.lightnessF() > 0.5) {
                // theme seems light
                backgroundColor = backgroundColor.lighter(101);
            } else {
                // theme seems dark
                backgroundColor = backgroundColor.darker(101);
            }
        }

        const QRectF backgroundRect(0, 0, size().width(), size().height());
        painter->fillRect(backgroundRect, backgroundColor);
    }

    const QStyle::State selectedState(m_selected ? QStyle::State_Selected : QStyle::State(0));
    if ((m_selected || m_current || (ngRingSelection() && m_ngSelProgress > 0.001)) && m_editedRole.isEmpty()) {
        const QStyle::State activeState(isActiveWindow() && widget->hasFocus() ? QStyle::State_Active : 0);
        // Only pass State_Selected when the item is actually selected, not just when
        // it has keyboard focus (m_current), to avoid styles drawing a persistent
        // selection highlight on focused but unselected items.
        drawItemStyleOption(painter, widget, activeState | selectedState | QStyle::State_Enabled | QStyle::State_Item);
    }

    if (m_hoverOpacity > 0.0 && ngNoHover()) {
        // nothing: no hover look on this item now
    } else if (m_hoverOpacity > 0.0 && ngRingSelection()) {
        // the ring is drawn around the item, outside its rect: straight onto the painter, not into the cache
        const QStyle::State activeState(isActiveWindow() && widget->hasFocus() ? QStyle::State_Active | QStyle::State_Enabled : 0);
        const qreal opacity = painter->opacity();
        painter->setOpacity(m_hoverOpacity * opacity);
        drawItemStyleOption(painter, widget, activeState | selectedState | QStyle::State_MouseOver | QStyle::State_Item);
        painter->setOpacity(opacity);
    } else if (m_hoverOpacity > 0.0) {
        if (!m_hoverCache) {
            // Initialize the m_hoverCache pixmap to improve the drawing performance
            // when fading the hover background
            m_hoverCache = new QPixmap(size().toSize());
            m_hoverCache->fill(Qt::transparent);

            QPainter pixmapPainter(m_hoverCache);
            const QStyle::State activeState(isActiveWindow() && widget->hasFocus() ? QStyle::State_Active | QStyle::State_Enabled : 0);
            drawItemStyleOption(&pixmapPainter, widget, activeState | selectedState | QStyle::State_MouseOver | QStyle::State_Item);
        }

        const qreal opacity = painter->opacity();
        painter->setOpacity(m_hoverOpacity * opacity);
        painter->drawPixmap(0, 0, *m_hoverCache);
        painter->setOpacity(opacity);
    }
}

void KItemListWidget::setVisibleRoles(const QList<QByteArray> &roles)
{
    const QList<QByteArray> previousRoles = m_visibleRoles;
    m_visibleRoles = roles;

    visibleRolesChanged(roles, previousRoles);
    update();
}

QList<QByteArray> KItemListWidget::visibleRoles() const
{
    return m_visibleRoles;
}

void KItemListWidget::setColumnWidth(const QByteArray &role, qreal width)
{
    const qreal previousWidth = m_columnWidths.value(role);
    if (previousWidth != width) {
        m_columnWidths.insert(role, width);
        columnWidthChanged(role, width, previousWidth);
        update();
    }
}

qreal KItemListWidget::columnWidth(const QByteArray &role) const
{
    return m_columnWidths.value(role);
}

void KItemListWidget::setSidePadding(qreal leftPaddingWidth, qreal rightPaddingWidth)
{
    bool changed = false;
    if (m_leftPadding != leftPaddingWidth) {
        m_leftPadding = leftPaddingWidth;
        changed = true;
    }

    if (m_rightPadding != rightPaddingWidth) {
        m_rightPadding = rightPaddingWidth;
        changed = true;
    }

    if (!changed) {
        return;
    }

    sidePaddingChanged(leftPaddingWidth, rightPaddingWidth);
    update();
}

qreal KItemListWidget::leftPadding() const
{
    return m_leftPadding;
}

qreal KItemListWidget::rightPadding() const
{
    return m_rightPadding;
}

void KItemListWidget::setStyleOption(const KItemListStyleOption &option)
{
    if (m_styleOption == option) {
        return;
    }

    const KItemListStyleOption previous = m_styleOption;
    clearHoverCache();
    m_styleOption = option;
    styleOptionChanged(option, previous);
    update();
}

const KItemListStyleOption &KItemListWidget::styleOption() const
{
    return m_styleOption;
}

void KItemListWidget::setSelected(bool selected)
{
    if (m_selected != selected) {
        m_selected = selected;
        if (m_selectionToggle) {
            m_selectionToggle->setChecked(selected);
        }
        selectedChanged(selected);
        clearHoverCache();
        update();
        if (ngRingSelection()) {
            // the ring comes out from under the card (or goes back under it), the bridges to its neighbours with it
            if (!m_ngSelAnim) {
                m_ngSelAnim = new QVariantAnimation(this);
                m_ngSelAnim->setDuration(150);
                m_ngSelAnim->setEasingCurve(QEasingCurve::OutCubic);
                connect(m_ngSelAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
                    m_ngSelProgress = value.toReal();
                    update();
                    if (parentItem()) {
                        const auto siblings = parentItem()->childItems();
                        for (QGraphicsItem *item : siblings) {
                            if (item != this && item->isVisible()) {
                                item->update();
                            }
                        }
                    }
                });
            }
            m_ngSelAnim->stop();
            m_ngSelAnim->setStartValue(m_ngSelProgress);
            m_ngSelAnim->setEndValue(selected ? 1.0 : 0.0);
            m_ngSelAnim->start();
        } else {
            m_ngSelProgress = selected ? 1.0 : 0.0;
        }
        if (ngRingSelection() && parentItem()) {
            // dolphin-ng: selected cards next to each other share one outline: the others redraw their part of it
            const auto siblings = parentItem()->childItems();
            for (QGraphicsItem *item : siblings) {
                if (item != this && item->isVisible()) {
                    item->update();
                }
            }
        }
    }
}

bool KItemListWidget::isSelected() const
{
    return m_selected;
}

void KItemListWidget::setCurrent(bool current)
{
    if (m_current != current) {
        m_current = current;
        currentChanged(current);
        update();
    }
}

bool KItemListWidget::isCurrent() const
{
    return m_current;
}

void KItemListWidget::setHovered(bool hovered)
{
    if (hovered == m_hovered) {
        return;
    }

    m_hovered = hovered;

    m_hoverSequenceIndex = 0;

    if (hovered) {
        setHoverOpacity(1.0);

        // dolphin-ng: a widget without a place for the toggle (the cards) gets none
        if (m_enabledSelectionToggle && !(QApplication::mouseButtons() & Qt::LeftButton) && !selectionToggleRect().isEmpty()) {
            initializeSelectionToggle();
        }

        hoverSequenceStarted();

        const KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
        const int interval = globalConfig.readEntry("HoverSequenceInterval", 700);

        m_hoverSequenceTimer.start(interval);
    } else {
        setHoverOpacity(0.0);

        if (m_selectionToggle) {
            m_selectionToggle->deleteLater();
            m_selectionToggle = nullptr;
        }

        hoverSequenceEnded();
        m_hoverSequenceTimer.stop();
    }

    hoveredChanged(hovered);
    update();
}

bool KItemListWidget::isHovered() const
{
    return m_hovered;
}

void KItemListWidget::setExpansionAreaHovered(bool hovered)
{
    if (hovered == m_expansionAreaHovered) {
        return;
    }
    m_expansionAreaHovered = hovered;
    update();
}

bool KItemListWidget::expansionAreaHovered() const
{
    return m_expansionAreaHovered;
}

void KItemListWidget::setHoverPosition(const QPointF &pos)
{
    if (m_selectionToggle) {
        m_selectionToggle->setHovered(selectionToggleRect().contains(pos));
    }
}

void KItemListWidget::setAlternateBackground(bool enable)
{
    if (m_alternateBackground != enable) {
        m_alternateBackground = enable;
        alternateBackgroundChanged(enable);
        update();
    }
}

bool KItemListWidget::alternateBackground() const
{
    return m_alternateBackground;
}

void KItemListWidget::setEnabledSelectionToggle(bool enable)
{
    if (m_enabledSelectionToggle != enable) {
        m_enabledSelectionToggle = enable;

        // We want the change to take effect immediately.
        if (m_enabledSelectionToggle) {
            if (m_hovered) {
                initializeSelectionToggle();
            }
        } else if (m_selectionToggle) {
            m_selectionToggle->deleteLater();
            m_selectionToggle = nullptr;
        }

        update();
    }
}

bool KItemListWidget::enabledSelectionToggle() const
{
    return m_enabledSelectionToggle;
}

void KItemListWidget::setSiblingsInformation(const QBitArray &siblings)
{
    const QBitArray previous = m_siblingsInfo;
    m_siblingsInfo = siblings;
    siblingsInformationChanged(m_siblingsInfo, previous);
    update();
}

QBitArray KItemListWidget::siblingsInformation() const
{
    return m_siblingsInfo;
}

void KItemListWidget::setEditedRole(const QByteArray &role)
{
    if (m_editedRole != role) {
        const QByteArray previous = m_editedRole;
        m_editedRole = role;
        editedRoleChanged(role, previous);
    }
}

QByteArray KItemListWidget::editedRole() const
{
    return m_editedRole;
}

void KItemListWidget::setIconSize(int iconSize)
{
    if (m_iconSize != iconSize) {
        const int previousIconSize = m_iconSize;
        m_iconSize = iconSize;
        iconSizeChanged(iconSize, previousIconSize);
    }
}

int KItemListWidget::iconSize() const
{
    return m_iconSize;
}

bool KItemListWidget::contains(const QPointF &point) const
{
    if (!QGraphicsWidget::contains(point)) {
        return false;
    }

    return selectionRectFull().contains(point) || expansionToggleRect().contains(point);
}

QRectF KItemListWidget::textFocusRect() const
{
    return textRect();
}

QRectF KItemListWidget::selectionToggleRect() const
{
    return QRectF();
}

QRectF KItemListWidget::expansionToggleRect() const
{
    return QRectF();
}

QPixmap KItemListWidget::createDragPixmap(const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QPixmap pixmap(size().toSize() * widget->devicePixelRatio());
    pixmap.setDevicePixelRatio(widget->devicePixelRatio());
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);

    const bool oldAlternateBackground = m_alternateBackground;
    const bool wasSelected = m_selected;
    const bool wasHovered = m_hovered;

    setAlternateBackground(false);
    setHovered(false);

    paint(&painter, option, widget);

    setAlternateBackground(oldAlternateBackground);
    setSelected(wasSelected);
    setHovered(wasHovered);

    return pixmap;
}

void KItemListWidget::startActivateSoonAnimation(int timeUntilActivation)
{
    Q_UNUSED(timeUntilActivation)
}

void KItemListWidget::dataChanged(const QHash<QByteArray, QVariant> &current, const QSet<QByteArray> &roles)
{
    Q_UNUSED(current)
    Q_UNUSED(roles)
}

void KItemListWidget::visibleRolesChanged(const QList<QByteArray> &current, const QList<QByteArray> &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::columnWidthChanged(const QByteArray &role, qreal current, qreal previous)
{
    Q_UNUSED(role)
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::sidePaddingChanged(qreal leftPaddingWidth, qreal rightPaddingWidth)
{
    Q_UNUSED(leftPaddingWidth)
    Q_UNUSED(rightPaddingWidth)
}

void KItemListWidget::styleOptionChanged(const KItemListStyleOption &current, const KItemListStyleOption &previous)
{
    Q_UNUSED(previous)

    // set the initial value of m_iconSize if not set
    if (m_iconSize == -1) {
        m_iconSize = current.iconSize;
    }
}

void KItemListWidget::currentChanged(bool current)
{
    Q_UNUSED(current)
}

void KItemListWidget::selectedChanged(bool selected)
{
    Q_UNUSED(selected)
}

void KItemListWidget::hoveredChanged(bool hovered)
{
    Q_UNUSED(hovered)
}

void KItemListWidget::pressedChanged(bool pressed)
{
    Q_UNUSED(pressed)
}

void KItemListWidget::alternateBackgroundChanged(bool enabled)
{
    Q_UNUSED(enabled)
}

void KItemListWidget::siblingsInformationChanged(const QBitArray &current, const QBitArray &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::editedRoleChanged(const QByteArray &current, const QByteArray &previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::iconSizeChanged(int current, int previous)
{
    Q_UNUSED(current)
    Q_UNUSED(previous)
}

void KItemListWidget::resizeEvent(QGraphicsSceneResizeEvent *event)
{
    QGraphicsWidget::resizeEvent(event);
    clearHoverCache();

    if (m_selectionToggle) {
        const QRectF &toggleRect = selectionToggleRect();
        m_selectionToggle->setPos(toggleRect.topLeft());
        m_selectionToggle->resize(toggleRect.size());
    }
}

void KItemListWidget::hoverSequenceStarted()
{
}

void KItemListWidget::hoverSequenceIndexChanged(int sequenceIndex)
{
    Q_UNUSED(sequenceIndex);
}

void KItemListWidget::hoverSequenceEnded()
{
}

qreal KItemListWidget::hoverOpacity() const
{
    return m_hoverOpacity;
}

int KItemListWidget::hoverSequenceIndex() const
{
    return m_hoverSequenceIndex;
}

void KItemListWidget::slotHoverSequenceTimerTimeout()
{
    m_hoverSequenceIndex++;
    hoverSequenceIndexChanged(m_hoverSequenceIndex);
}

void KItemListWidget::initializeSelectionToggle()
{
    Q_ASSERT(m_enabledSelectionToggle);

    if (!m_selectionToggle) {
        m_selectionToggle = new KItemListSelectionToggle(this);
    }

    const QRectF toggleRect = selectionToggleRect();
    m_selectionToggle->setPos(toggleRect.topLeft());
    m_selectionToggle->resize(toggleRect.size());

    m_selectionToggle->setChecked(isSelected());
}

void KItemListWidget::setHoverOpacity(qreal opacity)
{
    m_hoverOpacity = opacity;
    if (m_selectionToggle) {
        m_selectionToggle->setOpacity(opacity);
    }

    if (m_hoverOpacity <= 0.0) {
        delete m_hoverCache;
        m_hoverCache = nullptr;
    }

    update();
}

void KItemListWidget::clearHoverCache()
{
    delete m_hoverCache;
    m_hoverCache = nullptr;
}

bool KItemListWidget::isPressed() const
{
    return m_clickHighlighted;
}

void KItemListWidget::setPressed(bool enabled)
{
    if (m_clickHighlighted != enabled) {
        m_clickHighlighted = enabled;
        clearHoverCache();
        pressedChanged(enabled);
        update();
    }
}

bool KItemListWidget::ngRingSelection() const
{
    return false;
}

bool KItemListWidget::ngNoHover() const
{
    return false;
}

// dolphin-ng: the outline of the block of selected cards this one belongs to (cards next to each other, side by
// side or one above the other), in this item's coordinates. Every card of the block computes the same outline and
// draws the part of it inside its own reach (its rect and half of the gaps around it), so the pieces join.
static QPainterPath ngRoundedOutline(const QPainterPath &area, qreal radius)
{
    QPainterPath rounded;
    const QList<QPolygonF> polygons = area.toSubpathPolygons();
    for (QPolygonF polygon : polygons) {
        if (polygon.size() > 1 && polygon.first() == polygon.last()) {
            polygon.removeLast();
        }
        // drop the points in the middle of a straight run
        QPolygonF corners;
        for (int i = 0; i < polygon.size(); ++i) {
            const QPointF a = polygon.at((i + polygon.size() - 1) % polygon.size()), p = polygon.at(i), b = polygon.at((i + 1) % polygon.size());
            const bool straight = (qAbs(a.x() - p.x()) < 0.01 && qAbs(p.x() - b.x()) < 0.01) || (qAbs(a.y() - p.y()) < 0.01 && qAbs(p.y() - b.y()) < 0.01);
            if (!straight) {
                corners.append(p);
            }
        }
        const int n = corners.size();
        if (n < 3) {
            continue;
        }
        for (int i = 0; i < n; ++i) {
            const QPointF a = corners.at((i + n - 1) % n), p = corners.at(i), b = corners.at((i + 1) % n);
            const qreal la = QLineF(p, a).length(), lb = QLineF(p, b).length();
            const qreal r = qMin(radius, qMin(la, lb) / 2.0);
            const QPointF from = p + (a - p) * (r / la), to = p + (b - p) * (r / lb);
            if (i == 0) {
                rounded.moveTo(from);
            } else {
                rounded.lineTo(from);
            }
            rounded.quadTo(p, to);
        }
        rounded.closeSubpath();
    }
    return rounded;
}

QPainterPath KItemListWidget::ngSelectionBlock(qreal grow, QRectF *reach) const
{
    const QRectF own(pos(), size());
    QList<QRectF> cards{own};
    QList<qreal> progress{qMax<qreal>(m_ngSelProgress, 0.001)};
    if (QGraphicsItem *container = parentItem()) {
        const auto siblings = container->childItems();
        for (QGraphicsItem *item : siblings) {
            auto *other = dynamic_cast<KItemListWidget *>(item);
            if (other && other != this && other->isVisible() && other->ngSelectionProgress() > 0.001) {
                cards.append(QRectF(other->pos(), other->size()));
                progress.append(other->ngSelectionProgress());
            }
        }
    }
    auto beside = [](const QRectF &a, const QRectF &b) { // b just right of a on the same row
        return qAbs(a.top() - b.top()) < 1 && b.left() >= a.right() - 1 && b.left() - a.right() < 40;
    };
    auto under = [](const QRectF &a, const QRectF &b) { // b just below a in the same column
        return qAbs(a.left() - b.left()) < 1 && b.top() >= a.bottom() - 1 && b.top() - a.bottom() < 40;
    };
    // the block: the selected cards reachable from this one through neighbours
    QList<QRectF> block{own};
    QList<qreal> blockProgress{progress[0]};
    QList<bool> taken(cards.size(), false);
    taken[0] = true;
    for (int k = 0; k < block.size(); ++k) {
        for (int i = 0; i < cards.size(); ++i) {
            if (!taken[i] && (beside(block[k], cards[i]) || beside(cards[i], block[k]) || under(block[k], cards[i]) || under(cards[i], block[k]))) {
                taken[i] = true;
                block.append(cards[i]);
                blockProgress.append(progress[i]);
            }
        }
    }
    // a card on its way: its rect comes out from under the card (inset 10 px at 0, grown at 1), the bridges to
    // its neighbours open from the middle of the gap with the smaller of the two
    auto grownRect = [&](int i) {
        const qreal p = blockProgress[i], g = -10 + (grow + 10) * p;
        return block[i].adjusted(-g, -g, g, g);
    };
    QPainterPath area;
    area.setFillRule(Qt::WindingFill);
    for (int i = 0; i < block.size(); ++i) {
        const QRectF &a = block[i];
        area.addRect(grownRect(i));
        for (int j = 0; j < block.size(); ++j) {
            const QRectF &b = block[j];
            const qreal p = qMin(blockProgress[i], blockProgress[j]);
            if (p < 0.02) {
                continue;
            }
            if (beside(a, b)) {
                // the bridge grows from both cards towards the middle of the gap, and in height with the rings
                const qreal gap = b.left() - a.right(), half = gap / 2.0 * p, g = -10 + (grow + 10) * p;
                area.addRect(QRectF(a.right() - 1, a.top() - g, half + 1, a.height() + 2 * g));
                area.addRect(QRectF(b.left() - half, a.top() - g, half + 1, a.height() + 2 * g));
                // a 2x2 group: fill the gap where the four meet
                for (int k = 0; k < block.size(); ++k) {
                    if (under(a, block[k])) {
                        for (int l = 0; l < block.size(); ++l) {
                            if (under(b, block[l]) && qMin(p, qMin(blockProgress[k], blockProgress[l])) > 0.98) {
                                area.addRect(QRectF(a.right(), a.bottom(), gap, block[k].top() - a.bottom()));
                            }
                        }
                    }
                }
            }
            if (under(a, b)) {
                const qreal gap = b.top() - a.bottom(), half = gap / 2.0 * p, g = -10 + (grow + 10) * p;
                area.addRect(QRectF(a.left() - g, a.bottom() - 1, a.width() + 2 * g, half + 1));
                area.addRect(QRectF(a.left() - g, b.top() - half, a.width() + 2 * g, half + 1));
            }
        }
    }
    if (reach) {
        // this card's part: up to half of the gap towards a neighbour of the block, a little more elsewhere
        qreal left = grow + 3, right = grow + 3, top = grow + 3, bottom = grow + 3;
        for (const QRectF &b : std::as_const(block)) {
            if (beside(own, b)) {
                right = (b.left() - own.right()) / 2.0;
            }
            if (beside(b, own)) {
                left = (own.left() - b.right()) / 2.0;
            }
            if (under(own, b)) {
                bottom = (b.top() - own.bottom()) / 2.0;
            }
            if (under(b, own)) {
                top = (own.top() - b.bottom()) / 2.0;
            }
        }
        *reach = QRectF(-left, -top, own.width() + left + right, own.height() + top + bottom);
    }
    return ngRoundedOutline(area.simplified(), ngSelectionRadius() + grow).translated(-own.topLeft());
}

void KItemListWidget::ngPaintCheck(QPainter *painter, QWidget *widget)
{
    // the round check at the top left corner of a selected item with a ring selection, over its content
    if (!ngRingSelection() || m_ngSelProgress < 0.01) {
        return;
    }
    const qreal pop = m_ngSelProgress; // the check grows in with the ring
    const bool dark = ngIsDark(widget ? widget->palette() : m_styleOption.palette);
    QColor accent = m_styleOption.palette.color(QPalette::Highlight);
    if (dark && accent.lightness() < 150) {
        accent = QColor::fromHsl(accent.hslHue() < 0 ? 210 : accent.hslHue(), qMax(accent.hslSaturation(), 150), 150);
    }
    const QRectF card = selectionRectFull();
    const QPointF centre(card.left() + 10, card.top() + 10);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor(0, 0, 0, 90), 3));
    painter->setBrush(accent);
    painter->translate(centre);
    painter->scale(pop, pop);
    painter->translate(-centre);
    painter->drawEllipse(centre, 10, 10);
    painter->setPen(QPen(QColor::fromHsl(accent.hslHue() < 0 ? 0 : accent.hslHue(), 60, 22), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    QPainterPath tick;
    tick.moveTo(centre + QPointF(-4.2, 0.2));
    tick.lineTo(centre + QPointF(-1.2, 3.2));
    tick.lineTo(centre + QPointF(4.4, -2.8));
    painter->drawPath(tick);
    painter->restore();
}

bool KItemListWidget::ngOwnSelection() const
{
    return m_alternateBackground && ngTwoLineList();
}

qreal KItemListWidget::ngSelectionRadius() const
{
    return 9;
}

void KItemListWidget::drawItemStyleOption(QPainter *painter, QWidget *widget, QStyle::State styleState)
{
    if (ngOwnSelection()) {
        // dolphin-ng: rows and cards draw their own hover and selection, so the look does not depend on the widget
        // style. Hover is a neutral veil; the selection takes the accent colour, with an edge that catches the
        // light at the top left and fades towards the bottom right.
        const bool selected = styleState & QStyle::State_Selected;
        const bool hovered = styleState & QStyle::State_MouseOver;
        if (!selected && !hovered && !(ngRingSelection() && m_ngSelProgress > 0.001)) {
            return; // (a ring still going back under its card is drawn until it is gone)
        }
        const bool dark = ngIsDark(widget ? widget->palette() : m_styleOption.palette);
        const qreal radius = ngSelectionRadius();
        if (ngRingSelection()) {
            // cards: no fill (a tinted ground is what the bubble of files tied together looks like). A ring set off
            // from the card by a small gap, and a round check at its top left corner; hover gives a faint ring
            QColor accent = m_styleOption.palette.color(QPalette::Highlight);
            if (dark && accent.lightness() < 150) {
                accent = QColor::fromHsl(accent.hslHue() < 0 ? 210 : accent.hslHue(), qMax(accent.hslSaturation(), 150), 150);
            }
            const QRectF card = selectionRectFull();
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            QColor ring = accent;
            if (!selected) {
                ring.setAlphaF(0.38);
            }
            const qreal width = selected ? 2.5 : 1.5;
            const qreal gap = 2.5 + width / 2.0;
            if (selected || m_ngSelProgress > 0.001) {
                // one outline around the block of selected cards next to each other, with a faint veil inside
                QRectF reach;
                const QPainterPath block = ngSelectionBlock(gap, &reach);
                painter->setClipRect(reach, Qt::IntersectClip);
                QColor veil = accent;
                veil.setAlphaF(0.07);
                painter->setPen(QPen(ring, width));
                painter->setBrush(veil);
                painter->drawPath(block);
            } else {
                painter->setBrush(Qt::NoBrush);
                painter->setPen(QPen(ring, width));
                painter->drawRoundedRect(card.adjusted(-gap, -gap, gap, gap), radius + gap, radius + gap);
            }
            painter->restore(); // (the check is drawn over the card's content: ngPaintCheck)
            return;
        }
        const QRectF card = radius > 10 ? selectionRectFull() : selectionRectFull().adjusted(3, 3, -3, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (!selected) {
            QLinearGradient veil(card.topLeft(), card.topRight());
            const int base = dark ? 255 : 0;
            veil.setColorAt(0.0, QColor(base, base, base, dark ? 25 : 14));
            veil.setColorAt(1.0, QColor(base, base, base, dark ? 7 : 4));
            painter->setPen(Qt::NoPen);
            painter->setBrush(veil);
            painter->drawRoundedRect(card, radius, radius);
        } else {
            // the accent of the colour scheme, lifted when it is a dark one: as a faint fill it would not show
            QColor accent = m_styleOption.palette.color(QPalette::Highlight);
            if (dark && accent.lightness() < 150) {
                accent = QColor::fromHsl(accent.hslHue() < 0 ? 210 : accent.hslHue(), qMax(accent.hslSaturation(), 150), 150);
            }
            QColor fill = accent;
            fill.setAlphaF(hovered ? 0.34 : 0.26);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(card, radius, radius);
            const QRectF line = card.adjusted(0.6, 0.6, -0.6, -0.6);
            QColor bright = accent.lighter(dark ? 150 : 110), mid = accent, faint = accent;
            bright.setAlphaF(0.95);
            mid.setAlphaF(0.55);
            faint.setAlphaF(0.22);
            QLinearGradient edge(line.topLeft(), QPointF(line.left() + qMin(line.width(), line.height() * 6), line.bottom()));
            edge.setColorAt(0.0, bright);
            edge.setColorAt(0.35, mid);
            edge.setColorAt(1.0, faint);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(QBrush(edge), 1.2));
            painter->drawRoundedRect(line, radius, radius);
            // a hairline of light just inside the top edge
            painter->setClipRect(QRectF(card.left(), card.top(), card.width(), radius + 2), Qt::IntersectClip);
            painter->setPen(QPen(QColor(255, 255, 255, dark ? 30 : 110), 1));
            painter->drawRoundedRect(card.adjusted(1.6, 1.6, -1.6, -1.6), radius - 1, radius - 1);
        }
        painter->restore();
        return;
    }

    painter->save();
    QStyleOptionViewItem viewItemOption;
    initStyleOption(&viewItemOption);
    viewItemOption.state = styleState;
    viewItemOption.viewItemPosition = QStyleOptionViewItem::OnlyOne;
    viewItemOption.showDecorationSelected = true;
    viewItemOption.rect = selectionRectFull().toRect();
    const bool current = m_current && styleState & QStyle::State_Active;

    // TODO: Remove this check after Plasma 6.8 release
    // See: https://invent.kde.org/plasma/breeze/-/merge_requests/595
    if (style()->name() == QStringLiteral("breeze")) {
        QColor backgroundColor{widget->palette().color(QPalette::Highlight)};
        backgroundColor.setAlphaF(0.0);
        if (m_clickHighlighted) {
            backgroundColor.setAlphaF(1.0);
        } else {
            if (m_selected && m_hovered) {
                backgroundColor.setAlphaF(0.40);
            } else if (m_selected) {
                backgroundColor.setAlphaF(0.32);
            } else if (m_hovered) {
                backgroundColor = widget->palette().color(QPalette::Text);
                backgroundColor.setAlphaF(0.06);
            }
        }
        painter->setRenderHint(QPainter::Antialiasing);
        constexpr int roundness = 5; // From Breeze style.
        constexpr qreal penWidth = 1.25;
        QPainterPath path;
        const qreal adjustment = 0.5 * penWidth; // Use same adjustments as Breeze strokedRect uses, to snap to pixelGrid.
        path.addRoundedRect(selectionRectFull().adjusted(adjustment, adjustment, -adjustment, -adjustment), roundness, roundness);
        painter->fillPath(path, backgroundColor);

        // Focus decoration
        if (current) {
            QColor focusColor{widget->palette().color(QPalette::Highlight)};
            // Set the pen color lighter or darker depending on background color
            focusColor = m_styleOption.palette.color(QPalette::Base).lightnessF() > 0.5 ? focusColor.darker(110) : focusColor.lighter(110);
            focusColor.setAlphaF(m_selected || m_hovered ? 1.0 : 0.8);
            QPen pen{focusColor, penWidth};
            pen.setCosmetic(true);
            painter->strokePath(path, pen);
        }
    } else {
        style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &viewItemOption, painter, widget);

        // Focus decoration (dolphin-ng: not in the two-line list, where it shows as a light line under the row)
        if (current && !(m_alternateBackground && ngTwoLineList())) {
            QStyleOptionFocusRect focusRectOption;
            initStyleOption(&focusRectOption);
            focusRectOption.state = QStyle::State_HasFocus;
            if (m_selected && widget->hasFocus()) {
                focusRectOption.state = QStyle::State_HasFocus | QStyle::State_Selected;
            }
            if (m_hovered) {
                 focusRectOption.state |= QStyle::State_MouseOver;
            }
            focusRectOption.rect = viewItemOption.rect;
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focusRectOption, painter, widget);
        }
    }
    painter->restore();
}

#include "moc_kitemlistwidget.cpp"

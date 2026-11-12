/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2013 Frank Reininghaus <frank78ac@googlemail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kdirectorycontentscounter.h"
#include "dolphin_contentdisplaysettings.h"
#include "kitemviews/kfileitemmodel.h"

#include <KDirNotify>
#include <KDirWatch>

#include <QPointer>

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace
{

/**
 * dolphin-ng: what is known of each folder's contents (number of entries, size, newest modification inside), kept
 * across folder changes and across restarts (~/.cache/dolphin-ng/folder-sizes). A value shown from here is checked
 * again in the background each time the folder is shown (not within a minute of the last count); the numbers on
 * screen change only if the result differs. A change reported inside a folder (by KIO, or found by a count) makes
 * it and the folders above it out of date; those on screen are counted again at once.
 */
class LocalCache : public QObject
{
public:
    struct cacheData {
        int count = 0;
        long long size = 0;
        qint64 newest = 0; // seconds since the epoch, 0 when unknown
        qint64 timestamp = 0; // when it was counted (ms); 1 = known but to be counted again
        bool hidden = false; // counted with the hidden files

        inline operator bool() const
        {
            return timestamp != 0 && count != -1;
        }
    };

    static constexpr int kMaxEntries = 40000;

    LocalCache()
    {
        m_recheckTimer.setSingleShot(true);
        m_recheckTimer.setInterval(400);
        QObject::connect(&m_recheckTimer, &QTimer::timeout, this, [this] {
            const QSet<QString> paths = std::exchange(m_recheck, {});
            m_views.removeAll(nullptr);
            for (const auto &view : std::as_const(m_views)) {
                view->recheck(paths);
            }
        });
        // what KIO changes (copies, moves, deletions, renames, done by Dolphin or any KDE application) reaches the
        // folders above it at once; changes made by other programs are found when a folder is looked at again
        auto *notify = new OrgKdeKDirNotifyInterface(QString(), QString(), QDBusConnection::sessionBus(), this);
        auto parentOf = [](const QString &url) {
            const QUrl u(url);
            return u.isLocalFile() ? QFileInfo(u.adjusted(QUrl::StripTrailingSlash).toLocalFile()).path() : QString();
        };
        auto inFolder = [this](const QString &folder) {
            if (!folder.isEmpty()) {
                const QString canonical = QFileInfo(folder).canonicalFilePath();
                changedInside(canonical.isEmpty() ? folder : canonical);
            }
        };
        QObject::connect(notify, &OrgKdeKDirNotifyInterface::FilesAdded, this, [inFolder](const QString &directory) {
            const QUrl url(directory);
            if (url.isLocalFile()) {
                inFolder(url.adjusted(QUrl::StripTrailingSlash).toLocalFile());
            }
        });
        QObject::connect(notify, &OrgKdeKDirNotifyInterface::FilesRemoved, this, [inFolder, parentOf](const QStringList &files) {
            for (const QString &file : files) {
                inFolder(parentOf(file));
            }
        });
        QObject::connect(notify, &OrgKdeKDirNotifyInterface::FilesChanged, this, [inFolder, parentOf](const QStringList &files) {
            for (const QString &file : files) {
                inFolder(parentOf(file));
            }
        });
        auto moved = [inFolder, parentOf](const QString &src, const QString &dst) {
            inFolder(parentOf(src));
            inFolder(parentOf(dst));
        };
        QObject::connect(notify, &OrgKdeKDirNotifyInterface::FileRenamed, this, moved);
        QObject::connect(notify, &OrgKdeKDirNotifyInterface::FileMoved, this, moved);
        QObject::connect(notify, &OrgKdeKDirNotifyInterface::FileRenamedWithLocalPath, this, [moved](const QString &src, const QString &dst, const QString &) {
            moved(src, dst);
        });

        m_saveTimer.setSingleShot(true);
        m_saveTimer.setInterval(4000);
        QObject::connect(&m_saveTimer, &QTimer::timeout, this, [this] {
            save();
        });
        if (QCoreApplication::instance()) {
            QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
                if (m_dirty) {
                    save();
                }
            });
        }
        load();
    }

    cacheData insert(const QString &key, cacheData data)
    {
        data.timestamp = QDateTime::currentMSecsSinceEpoch();
        if (data.newest == 0) {
            data.newest = m_cache.value(key).newest;
        }
        m_cache.insert(key, data);
        changed();
        return data;
    }

    void setNewest(const QString &key, qint64 newest)
    {
        auto it = m_cache.find(key);
        if (it != m_cache.end() && it->newest != newest) {
            it->newest = newest;
            changed();
        }
    }

    cacheData value(const QString &key) const
    {
        return m_cache.value(key);
    }

    // still shown, but counted again at the next look (a refresh)
    void markStale(const QSet<QString> &keys)
    {
        for (const auto &key : keys) {
            auto it = m_cache.find(key);
            if (it != m_cache.end()) {
                it->timestamp = 1;
            }
        }
    }

    // something changed inside the folder at @p path: it and every folder above it are counted again, those on
    // screen right away (the views are told after a short pause, so that a copy of many files asks once)
    void changedInside(const QString &path, bool includingItself = true)
    {
        QString folder = path;
        bool first = true;
        while (!folder.isEmpty()) {
            if (!first || includingItself) {
                auto it = m_cache.find(folder);
                if (it != m_cache.end() && it->timestamp != 1) {
                    it->timestamp = 1;
                    m_recheck.insert(folder);
                }
            }
            first = false;
            if (folder == QLatin1String("/")) {
                break;
            }
            const QString parent = QFileInfo(folder).path();
            if (parent == folder) {
                break;
            }
            folder = parent;
        }
        if (!m_recheck.isEmpty()) {
            m_recheckTimer.start();
        }
    }

    void addView(KDirectoryContentsCounter *counter)
    {
        m_views.append(counter);
    }

private:
    static QString file()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/dolphin-ng/folder-sizes");
    }

    void changed()
    {
        m_dirty = true;
        m_saveTimer.start();
    }

    void load()
    {
        QFile in(file());
        if (!in.open(QIODevice::ReadOnly)) {
            return;
        }
        QDataStream stream(&in);
        quint32 magic = 0, version = 0, entries = 0;
        stream >> magic >> version >> entries;
        if (magic != 0x4e47465a || version != 1) {
            return;
        }
        m_cache.reserve(int(entries));
        for (quint32 i = 0; i < entries && stream.status() == QDataStream::Ok; ++i) {
            QString path;
            cacheData data;
            qint32 count = 0;
            qint64 size = 0;
            stream >> path >> count >> size >> data.newest >> data.timestamp >> data.hidden;
            data.count = count;
            data.size = size;
            if (stream.status() == QDataStream::Ok && data) {
                m_cache.insert(path, data);
            }
        }
    }

    void save()
    {
        m_dirty = false;
        // the oldest counts go first when there are too many
        if (m_cache.size() > kMaxEntries) {
            QList<qint64> stamps;
            stamps.reserve(m_cache.size());
            for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
                stamps.append(it->timestamp);
            }
            std::nth_element(stamps.begin(), stamps.begin() + (stamps.size() - kMaxEntries), stamps.end());
            const qint64 cut = stamps.at(stamps.size() - kMaxEntries);
            for (auto it = m_cache.begin(); it != m_cache.end();) {
                it = it->timestamp < cut ? m_cache.erase(it) : std::next(it);
            }
        }
        const QString path = file();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly)) {
            return;
        }
        QDataStream stream(&out);
        stream << quint32(0x4e47465a) << quint32(1) << quint32(m_cache.size());
        for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
            stream << it.key() << qint32(it->count) << qint64(it->size) << it->newest << it->timestamp << it->hidden;
        }
        out.commit();
    }

    QHash<QString, cacheData> m_cache;
    QTimer m_saveTimer;
    bool m_dirty = false;
    QSet<QString> m_recheck; // folders to count again, for the views that show them
    QTimer m_recheckTimer;
    QList<QPointer<KDirectoryContentsCounter>> m_views;
};

/// cache of directory counting result
static LocalCache *s_cache;
static QThread *s_workerThread;
static KDirectoryContentsCounterWorker *s_worker;

// dolphin-ng: whether a cached count is surely out of date: unknown, the folder changed since (or a change inside it
// was reported), or it was counted with other options. Such a folder is counted first; any other one shown is
// checked again too, quietly, behind them (a change deep inside does not touch the folder's own date)
bool needsCount(const LocalCache::cacheData &data, const QFileInfo &info, bool hidden)
{
    if (!data) {
        return true;
    }
    return data.timestamp < info.fileTime(QFile::FileModificationTime).toMSecsSinceEpoch() || data.hidden != hidden;
}

// counted less than a minute ago and nothing known changed since: going in and out of a folder does not count again
bool countedJustNow(const LocalCache::cacheData &data, const QFileInfo &info, bool hidden)
{
    return !needsCount(data, info, hidden) && QDateTime::currentMSecsSinceEpoch() - data.timestamp < 60 * 1000;
}
}

KDirectoryContentsCounter::KDirectoryContentsCounter(KFileItemModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_priorityQueue()
    , m_queue()
    , m_workerIsBusy(false)
    , m_dirWatcher(nullptr)
    , m_watchedDirs()
{
    if (s_cache == nullptr) {
        s_cache = new LocalCache();
    }
    s_cache->addView(this);

    if (!s_workerThread) {
        s_workerThread = new QThread();
        s_workerThread->setObjectName(QStringLiteral("KDirectoryContentsCounterThread"));
        s_workerThread->start(QThread::LowPriority); // dolphin-ng: counting is background work
    }

    if (!s_worker) {
        s_worker = new KDirectoryContentsCounterWorker();
        s_worker->moveToThread(s_workerThread);
    }

    connect(m_model, &KFileItemModel::itemsRemoved, this, &KDirectoryContentsCounter::slotItemsRemoved);
    connect(m_model, &KFileItemModel::directoryRefreshing, this, &KDirectoryContentsCounter::slotDirectoryRefreshing);

    connect(this, &KDirectoryContentsCounter::requestDirectoryContentsCount, s_worker, &KDirectoryContentsCounterWorker::countDirectoryContents);

    connect(s_worker, &KDirectoryContentsCounterWorker::result, this, &KDirectoryContentsCounter::slotResult);
    // dolphin-ng: the growing numbers of a count in progress only where nothing was known before; a known value stays
    // on screen until the new count is complete
    connect(s_worker, &KDirectoryContentsCounterWorker::intermediateResult, this, [this](const QString &path, int count, long long size) {
        if (!m_shown.contains(path)) {
            Q_EMIT result(path, count, size);
        }
    });
    connect(s_worker, &KDirectoryContentsCounterWorker::newestModified, this, [this](const QString &path, qint64 secs) {
        m_pendingNewest.insert(path, secs);
        Q_EMIT newestModified(path, secs);
    });
    connect(s_worker, &KDirectoryContentsCounterWorker::finished, this, &KDirectoryContentsCounter::scheduleNext);

    m_dirWatcher = new KDirWatch(this);
    connect(m_dirWatcher, &KDirWatch::dirty, this, &KDirectoryContentsCounter::slotDirWatchDirty);
}

KDirectoryContentsCounter::~KDirectoryContentsCounter()
{
}

void KDirectoryContentsCounter::slotResult(const QString &path, int count, long long size)
{
    const auto fileInfo = QFileInfo(path);
    const QString resolvedPath = fileInfo.canonicalFilePath();
    if (fileInfo.isReadable() && !m_watchedDirs.contains(resolvedPath)) {
        m_dirWatcher->addDir(resolvedPath);
    }
    m_watchedDirs.insert(resolvedPath);

    // update cache or overwrite value; the numbers on screen change only if they differ
    const LocalCache::cacheData before = s_cache->value(resolvedPath);
    if (before && (before.count != count || before.size != size)) {
        s_cache->changedInside(resolvedPath, false); // the folders above it hold it: their counts are out of date too
    }
    LocalCache::cacheData data;
    data.count = count;
    data.size = size;
    data.newest = m_pendingNewest.take(path);
    data.hidden = m_model->showHiddenFiles();
    s_cache->insert(resolvedPath, data);

    showResult(path, count, size);
}

// dolphin-ng: each view is told a value only when it differs from what it shows (the worker and the cache are shared
// by all views; a known value is not replaced by the growing numbers of a new count)
void KDirectoryContentsCounter::showResult(const QString &path, int count, long long size, bool queued)
{
    const auto shown = m_shown.constFind(path);
    if (shown != m_shown.constEnd() && shown->first == count && shown->second == size) {
        return;
    }
    m_shown.insert(path, {count, size});
    if (queued) {
        // a value from the cache is known at once, while the roles updater is still filling in the item: told right
        // away, it would be overwritten by the folder's own (empty) size
        QMetaObject::invokeMethod(
            this,
            [this, path, count, size] {
                Q_EMIT result(path, count, size);
            },
            Qt::QueuedConnection);
        return;
    }
    Q_EMIT result(path, count, size);
}

void KDirectoryContentsCounter::slotDirWatchDirty(const QString &path)
{
    const int index = m_model->index(QUrl::fromLocalFile(path));
    if (index >= 0) {
        if (!m_model->fileItem(index).isDir()) {
            // If INotify is used, KDirWatch issues the dirty() signal
            // also for changed files inside the directory, even if we
            // don't enable this behavior explicitly (see bug 309740).
            return;
        }

        scanDirectory(path, PathCountPriority::High);
    }
}

void KDirectoryContentsCounter::slotItemsRemoved()
{
    const bool allItemsRemoved = (m_model->count() == 0);

    if (allItemsRemoved) {
        stopWorker(); // dolphin-ng: what was counted stays known (LocalCache)
        m_shown.clear(); // a new folder: its items have shown nothing yet
    }

    if (!m_watchedDirs.isEmpty()) {
        // Don't let KDirWatch watch for removed items
        if (allItemsRemoved) {
            for (const QString &path : std::as_const(m_watchedDirs)) {
                m_dirWatcher->removeDir(path);
            }
            m_watchedDirs.clear();
        } else {
            QMutableSetIterator<QString> it(m_watchedDirs);
            while (it.hasNext()) {
                const QString &path = it.next();
                if (m_model->index(QUrl::fromLocalFile(path)) < 0) {
                    m_dirWatcher->removeDir(path);
                    it.remove();
                }
            }
        }
    }
}

void KDirectoryContentsCounter::slotDirectoryRefreshing()
{
    s_cache->markStale(m_watchedDirs); // shown until counted again
}

void KDirectoryContentsCounter::scheduleNext()
{
    if (!m_priorityQueue.empty()) {
        m_currentPath = m_priorityQueue.front();
        m_priorityQueue.pop_front();
    } else if (!m_queue.empty()) {
        m_currentPath = m_queue.front();
        m_queue.pop_front();
    } else {
        m_currentPath.clear();
        m_workerIsBusy = false;
        return;
    }

    const auto fileInfo = QFileInfo(m_currentPath);
    const QString resolvedPath = fileInfo.canonicalFilePath();
    const auto pair = s_cache->value(resolvedPath);
    if (pair) {
        // fast path when in cache
        // will be updated later if result has changed
        showResult(m_currentPath, pair.count, pair.size, true);
        if (pair.newest > 0) {
            const QString path = m_currentPath;
            const qint64 newest = pair.newest;
            QMetaObject::invokeMethod(
                this,
                [this, path, newest] {
                    Q_EMIT newestModified(path, newest);
                },
                Qt::QueuedConnection);
        }
    }

    // counted a moment ago (by another view, say) and nothing changed since: skip
    if (countedJustNow(pair, fileInfo, m_model->showHiddenFiles())) {
        scheduleNext();
        return;
    }

    KDirectoryContentsCounterWorker::Options options;

    if (m_model->showHiddenFiles()) {
        options |= KDirectoryContentsCounterWorker::CountHiddenFiles;
    }

    m_workerIsBusy = true;
    Q_EMIT requestDirectoryContentsCount(m_currentPath, options, ContentDisplaySettings::recursiveDirectorySizeLimit());
}

void KDirectoryContentsCounter::enqueuePathScanning(const QString &path, bool alreadyInCache, PathCountPriority priority)
{
    // ensure to update the entry in the queue
    auto it = std::find(m_queue.begin(), m_queue.end(), path);
    if (it != m_queue.end()) {
        m_queue.erase(it);
    } else {
        it = std::find(m_priorityQueue.begin(), m_priorityQueue.end(), path);
        if (it != m_priorityQueue.end()) {
            m_priorityQueue.erase(it);
        }
    }

    if (priority == PathCountPriority::Normal) {
        if (alreadyInCache) {
            // we already knew the dir size
            // otherwise it gets lower priority
            m_queue.push_back(path);
        } else {
            m_queue.push_front(path);
        }
    } else {
        // append to priority queue
        m_priorityQueue.push_front(path);
    }
}

void KDirectoryContentsCounter::scanDirectory(const QString &path, PathCountPriority priority)
{
    if (m_workerIsBusy && m_currentPath == path) {
        // already listing
        return;
    }

    const auto fileInfo = QFileInfo(path);
    const QString resolvedPath = fileInfo.canonicalFilePath();
    const auto pair = s_cache->value(resolvedPath);
    if (pair) {
        // fast path when in cache
        // will be updated later if result has changed
        showResult(path, pair.count, pair.size, true);
        if (pair.newest > 0) {
            const qint64 newest = pair.newest;
            QMetaObject::invokeMethod(
                this,
                [this, path, newest] {
                    Q_EMIT newestModified(path, newest);
                },
                Qt::QueuedConnection);
        }
    }

    // counted a moment ago and nothing changed since: nothing to do. Known to be out of date: counted first.
    // Otherwise checked again quietly, after everything else; the numbers change only if they differ
    const bool hidden = m_model->showHiddenFiles();
    if (countedJustNow(pair, fileInfo, hidden)) {
        return;
    }
    enqueuePathScanning(path, pair, needsCount(pair, fileInfo, hidden) ? priority : PathCountPriority::Normal);

    if (!m_workerIsBusy && !s_worker->stopping()) {
        scheduleNext();
    }
}

void KDirectoryContentsCounter::recheck(const QSet<QString> &paths)
{
    // the folders of this view among them are counted again (the cache already marked them out of date)
    for (const QString &path : paths) {
        if (m_model->index(QUrl::fromLocalFile(path)) >= 0) {
            scanDirectory(path, PathCountPriority::High);
        }
    }
}

void KDirectoryContentsCounter::stopWorker()
{
    m_queue.clear();
    m_priorityQueue.clear();

    if (m_workerIsBusy && m_currentPath == s_worker->scannedPath()) {
        s_worker->stop();
    }
    m_currentPath.clear();
}

#include "moc_kdirectorycontentscounter.cpp"

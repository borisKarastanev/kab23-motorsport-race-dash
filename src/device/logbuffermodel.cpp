#include "src/device/logbuffermodel.h"
#include "src/core/apppaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <utility>

LogBufferModel *LogBufferModel::s_instance = nullptr;
QtMessageHandler LogBufferModel::s_previousHandler = nullptr;

namespace {
thread_local bool g_inHandler = false;
}

QString LogBufferModel::logPath()
{
    return AppPaths::dataFile("dash.log");
}

QString LogBufferModel::rotatedLogPath()
{
    return AppPaths::dataFile("dash.log.1");
}

LogBufferModel::LogBufferModel(QObject *parent) : QObject(parent)
{
    s_instance = this;

    loadPersisted();

    m_logFile.setFileName(logPath());
    m_logFile.open(QIODevice::Append | QIODevice::Text);
    m_logBytes = m_logFile.size();

    refresh(); // seed the snapshot with the persisted entries

    // The writer. All log-file I/O happens here, on the main thread, outside
    // m_mutex — see the class comment. 10 Hz batches a flood into one write burst
    // and one flush instead of a flush per message.
    m_writeTimer.setInterval(100);
    connect(&m_writeTimer, &QTimer::timeout, this, &LogBufferModel::drainPending);
    m_writeTimer.start();

    s_previousHandler = qInstallMessageHandler(&LogBufferModel::messageHandler);
}

LogBufferModel::~LogBufferModel()
{
    qInstallMessageHandler(s_previousHandler);
    s_instance = nullptr;
    // Persist the tail of the log, but via flushToDisk() rather than drainPending():
    // emitting newSinceRefreshChanged() here would dispatch into QML bindings on an
    // object that is already half-destroyed.
    flushToDisk();
}

std::deque<LogBufferModel::Entry> &LogBufferModel::bucketFor(const QString &level)
{
    if (level == "warn")
        return m_warn;
    if (level == "error")
        return m_error;
    return m_info;
}

const std::deque<LogBufferModel::Entry> &LogBufferModel::bucketFor(const QString &level) const
{
    if (level == "warn")
        return m_warn;
    if (level == "error")
        return m_error;
    return m_info;
}

void LogBufferModel::pushCapped(std::deque<Entry> &bucket, const Entry &e)
{
    bucket.push_back(e);
    if (bucket.size() > static_cast<size_t>(kCapacity))
        bucket.pop_front();
}

QByteArray LogBufferModel::serializeLine(const QString &level, const QString &category,
                                         const QString &message, const QDateTime &when)
{
    QJsonObject obj;
    obj["t"] = when.toString(Qt::ISODate);
    obj["l"] = level;
    obj["c"] = category;
    obj["m"] = message;

    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

void LogBufferModel::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (s_instance && !g_inHandler) {
        g_inHandler = true;
        s_instance->handleMessage(type, context, msg);
        g_inHandler = false;
    }

    if (s_previousHandler)
        s_previousHandler(type, context, msg);
}

void LogBufferModel::handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString level;
    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
        level = "info";
        break;
    case QtWarningMsg:
        level = "warn";
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = "error";
        break;
    }

    const QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");
    const QDateTime now = QDateTime::currentDateTime();

    Entry e;
    e.time     = now.toString("HH:mm:ss");
    e.category = category;
    e.message  = msg;
    e.prev     = false;

    // Serializing is the most expensive step per message, so skip it entirely when
    // the writer is already saturated — appendEntry() would only drop the result.
    // The entry still goes into the in-memory bucket. Done before the lock: pure
    // computation, no I/O, no contention.
    QByteArray line;
    if (!m_writeQueueFull.load(std::memory_order_relaxed))
        line = serializeLine(level, category, msg, now);

    appendEntry(level, e, std::move(line));
}

void LogBufferModel::appendEntry(const QString &level, const Entry &e, QByteArray &&line)
{
    {
        QMutexLocker locker(&m_mutex);

        pushCapped(bucketFor(level), e);

        // An empty line means handleMessage() skipped serialization because the queue
        // looked full; re-check the real depth here, under the lock.
        if (line.isEmpty() || m_pendingWrites.size() >= kMaxPendingWrites) {
            ++m_droppedWrites;
            m_writeQueueFull.store(true, std::memory_order_relaxed);
        } else {
            m_pendingWrites.append(std::move(line));
        }

        // Only counts toward the refresh badge if it lands in the on-screen bucket —
        // an "error" line shouldn't nag while "info" is selected.
        if (level != m_filterLevel)
            return;
    }
    m_newSinceRefresh.fetch_add(1, std::memory_order_relaxed);
}

void LogBufferModel::writeTracked(const QByteArray &line)
{
    const qint64 written = m_logFile.write(line);
    if (written > 0) // write() returns -1 on error; adding that would drive m_logBytes
        m_logBytes += written; // negative and stop rotateIfNeeded() from ever firing
}

void LogBufferModel::flushToDisk()
{
    // Check BEFORE consuming the queue. If the file isn't open (read-only rootfs,
    // full disk, or a failed re-open inside rotateIfNeeded), swapping the queue out
    // here would silently destroy every line we couldn't write.
    if (!m_logFile.isOpen())
        return;

    QList<QByteArray> writes;
    qint64 dropped = 0;
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingWrites.isEmpty() && m_droppedWrites == 0)
            return;
        writes.swap(m_pendingWrites);
        dropped = std::exchange(m_droppedWrites, qint64(0));
        m_writeQueueFull.store(false, std::memory_order_relaxed);
    }

    // File I/O, outside the lock. One write burst and a single flush per tick rather
    // than a flush per message: on a power cut we lose at most ~100 ms of log, which
    // is a far better trade than holding m_mutex across SD-card I/O and freezing the
    // UI thread that is trying to log (and to draw this very page).
    for (const QByteArray &line : writes)
        writeTracked(line);

    if (dropped > 0) {
        const QDateTime now = QDateTime::currentDateTime();
        const QString note =
            QStringLiteral("%1 message(s) dropped — log flood outran the writer").arg(dropped);

        // Into the in-memory buffer as well as the file: someone reading Device Log
        // *on the device* — the whole point of this page — must see that lines were
        // suppressed, not just a plausible-looking list with holes in it.
        //
        // Pushed directly rather than through appendEntry(), which would count this
        // note itself as a dropped write (it carries no queued line — we write it
        // below by hand) and so regenerate a fresh note on every tick, forever.
        Entry e;
        e.time     = now.toString("HH:mm:ss");
        e.category = QStringLiteral("dash.log");
        e.message  = note;
        e.prev     = false;

        bool visible = false;
        {
            QMutexLocker locker(&m_mutex);
            pushCapped(m_warn, e);
            visible = (m_filterLevel == QLatin1String("warn"));
        }
        if (visible)
            m_newSinceRefresh.fetch_add(1, std::memory_order_relaxed);

        writeTracked(serializeLine(QStringLiteral("warn"), e.category, note, now));
    }

    m_logFile.flush();
    rotateIfNeeded();
}

void LogBufferModel::drainPending()
{
    flushToDisk();

    const int newCount = m_newSinceRefresh.load(std::memory_order_relaxed);
    if (newCount != m_lastNotifiedNew) {
        m_lastNotifiedNew = newCount;
        emit newSinceRefreshChanged();
    }
}

void LogBufferModel::rotateIfNeeded()
{
    // Tracked byte counter instead of a size() stat on every message.
    if (m_logBytes < kRotateBytes)
        return;

    m_logFile.close();
    QFile::remove(rotatedLogPath());
    QFile::rename(logPath(), rotatedLogPath());
    m_logFile.setFileName(logPath());
    m_logFile.open(QIODevice::Append | QIODevice::Text);
    m_logBytes = 0;
}

void LogBufferModel::loadPersisted()
{
    // Collect raw lines (cheap I/O) oldest-file-first, then JSON-parse only from
    // the newest backwards, stopping once every bucket is full — so a large log
    // isn't fully parsed on the startup path just to keep the last kCapacity per level.
    QStringList lines;
    for (const QString &path : { rotatedLogPath(), logPath() }) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream in(&f);
        while (!in.atEnd())
            lines.append(in.readLine());
    }

    // Newest-first temporaries; reversed into the members afterwards so the
    // deques stay in chronological (oldest-front) order.
    std::deque<Entry> info, warn, error;
    auto full = [](const std::deque<Entry> &d) { return d.size() >= static_cast<size_t>(kCapacity); };

    for (int i = lines.size() - 1; i >= 0; --i) {
        if (full(info) && full(warn) && full(error))
            break;

        const QString &line = lines.at(i);
        if (line.trimmed().isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        std::deque<Entry> &bucket = obj["l"].toString() == "warn"  ? warn
                                  : obj["l"].toString() == "error" ? error
                                  :                                  info;
        if (full(bucket))
            continue;

        const QDateTime dt = QDateTime::fromString(obj["t"].toString(), Qt::ISODate);
        Entry e;
        e.time     = dt.isValid() ? dt.toString("HH:mm:ss") : QString();
        e.category = obj["c"].toString();
        e.message  = obj["m"].toString();
        e.prev     = true;
        bucket.push_back(e); // newest-first for now
    }

    for (auto pair : { std::pair(&info, &m_info), std::pair(&warn, &m_warn), std::pair(&error, &m_error) })
        pair.second->assign(pair.first->rbegin(), pair.first->rend());
}

QVariantMap LogBufferModel::toVariant(const Entry &e)
{
    QVariantMap m;
    m["time"]     = e.time;
    m["category"] = e.category;
    m["message"]  = e.message;
    m["prev"]     = e.prev;
    return m;
}

void LogBufferModel::setFilterLevel(const QString &level)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_filterLevel == level)
            return;
        m_filterLevel = level;
    }
    emit filterLevelChanged();
    refresh(); // switching bucket is an explicit user action — show it immediately
}

void LogBufferModel::setLimit(int limit)
{
    // QML can write any int to this property. A negative would make refresh() form
    // `bucket.end() - n` past the end and walk off the deque (SIGSEGV); anything above
    // kCapacity is unserviceable because the buckets hold no more.
    const int clamped = std::clamp(limit, 0, kCapacity);
    if (m_limit == clamped)
        return;
    m_limit = clamped;
    emit limitChanged();
    refresh();
}

void LogBufferModel::refresh()
{
    QVariantList snapshot;
    {
        QMutexLocker locker(&m_mutex);
        const std::deque<Entry> &bucket = bucketFor(m_filterLevel);
        const int n = std::min<int>(m_limit, static_cast<int>(bucket.size()));
        snapshot.reserve(n);
        for (auto it = bucket.end() - n; it != bucket.end(); ++it)
            snapshot.append(toVariant(*it));
    }

    m_newSinceRefresh.store(0, std::memory_order_relaxed);
    m_snapshot = std::move(snapshot);
    m_lastNotifiedNew = 0;
    emit entriesChanged();
    emit newSinceRefreshChanged();
}

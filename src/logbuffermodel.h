#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QFile>
#include <QTimer>
#include <atomic>
#include <deque>

// Installs a Qt message handler and buffers the last kCapacity messages per
// level (info / warn / error) for display in the Device Log settings page.
// Messages are also appended to dash.log as JSON lines so the log survives
// restarts and crashes; the previous run's entries are loaded on startup and
// marked with "prev": true. The default handler is chained so stderr/journald
// output is unaffected.
//
// THREADING / WHY THIS IS SHAPED THIS WAY
// The message handler runs on arbitrary threads, including the UI thread (Qt and
// QML log from it constantly). It must therefore never block on I/O: an earlier
// version wrote + flushed the log file and rotated it *while holding m_mutex*,
// which meant every qWarning() and every read of the entry list — i.e. the whole
// UI thread — blocked on SD-card I/O. Under a log flood that starved the main
// thread and froze the app, which is what opening the Device Log page triggered.
//
// So: handleMessage() only touches memory under a short lock. All file I/O is
// batched and performed by flushToDisk() on the main thread's timer, outside the
// lock. The page is pull-based (entries() + refresh()) rather than bound live to
// the buffer, so a flood can't drive QML rebuilds at all.
class LogBufferModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterLevelChanged)
    // How many of the most recent entries refresh() puts in the snapshot (20/50/100).
    // Clamped to [0, kCapacity] by setLimit() — QML can write any int here.
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    // The snapshot the page renders. Only changes on refresh() — NOT live-bound to
    // the incoming message stream, so a log flood cannot force QML relayouts.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    // Messages that landed in the visible bucket since the last refresh(), so the
    // refresh button can badge "N new" without rebuilding the list.
    Q_PROPERTY(int newSinceRefresh READ newSinceRefresh NOTIFY newSinceRefreshChanged)

public:
    explicit LogBufferModel(QObject *parent = nullptr);
    ~LogBufferModel() override;

    QString filterLevel() const { QMutexLocker l(&m_mutex); return m_filterLevel; }
    void setFilterLevel(const QString &level);

    int limit() const { return m_limit; }
    void setLimit(int limit);

    // Main-thread only, and deliberately lock-free: m_snapshot is written only by
    // refresh() (main thread), so QML can read it without contending with the
    // message handler at all.
    QVariantList entries() const { return m_snapshot; }
    // Atomic rather than mutex-guarded: the message handler bumps it from arbitrary
    // threads while QML reads it from the main thread, and taking m_mutex on a plain
    // property read is exactly the contention this class exists to avoid.
    int newSinceRefresh() const { return m_newSinceRefresh.load(std::memory_order_relaxed); }

    // Rebuilds the snapshot from the last limit() entries of the current bucket
    // and clears the "new since refresh" counter.
    Q_INVOKABLE void refresh();

signals:
    void filterLevelChanged();
    void limitChanged();
    void entriesChanged();
    void newSinceRefreshChanged();

private:
    struct Entry {
        QString time;
        QString category;
        QString message;
        bool prev = false;
    };

    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    // Memory-only, called from any thread. Appends to the level's bucket and queues
    // the already-serialized JSON line for the writer. Holds m_mutex for microseconds
    // and performs NO I/O — see the class comment. An empty `line` means the caller
    // skipped serialization because the write queue was already saturated, so the
    // entry is buffered in memory but counted as dropped for the file.
    void appendEntry(const QString &level, const Entry &e, QByteArray &&line);
    // Writes the queued lines to disk: one batched write + a single flush + at most
    // one rotation, all OUTSIDE m_mutex. Bails out without consuming the queue if the
    // file isn't open, so lines survive a failed (re)open and go out on a later tick.
    void flushToDisk();
    // Main-thread timer tick: flushToDisk() plus the newSinceRefresh notification.
    // Kept separate from flushToDisk() so the destructor can persist the tail of the
    // log without emitting signals into a half-destroyed object.
    void drainPending();
    void loadPersisted();
    void rotateIfNeeded();
    // Appends to m_logFile, ignoring QFile::write()'s -1 error return so a failing
    // disk can't drive m_logBytes negative and permanently disable rotation.
    void writeTracked(const QByteArray &line);
    static QString logPath();
    static QString rotatedLogPath();
    static QByteArray serializeLine(const QString &level, const QString &category,
                                    const QString &message, const QDateTime &when);
    static QVariantMap toVariant(const Entry &e);
    static void pushCapped(std::deque<Entry> &bucket, const Entry &e);
    std::deque<Entry> &bucketFor(const QString &level);
    const std::deque<Entry> &bucketFor(const QString &level) const;

    // ---- guarded by m_mutex (memory only; never held across I/O) ----
    mutable QMutex m_mutex;
    std::deque<Entry> m_info, m_warn, m_error;
    QString m_filterLevel = "info";
    // Serialized JSON lines awaiting the writer. Bounded: under a flood we would
    // otherwise queue faster than the SD card can drain and grow without limit, so
    // overflow is dropped and counted instead. A diagnostic ring buffer that OOMs
    // the app it is diagnosing is worse than one that admits it dropped lines.
    QList<QByteArray> m_pendingWrites;
    qint64 m_droppedWrites = 0;

    // ---- lock-free ----
    // Read by QML on the main thread, bumped by the handler on any thread.
    std::atomic<int> m_newSinceRefresh{0};
    // Lets handleMessage() skip the (expensive) JSON serialization for a line the
    // saturated writer would only throw away. Advisory only — appendEntry() re-checks
    // the real queue depth under the lock.
    std::atomic<bool> m_writeQueueFull{false};

    // ---- main thread only (no lock needed) ----
    QVariantList m_snapshot;
    int m_limit = 20;
    QFile m_logFile;
    qint64 m_logBytes = 0;
    int m_lastNotifiedNew = 0;
    QTimer m_writeTimer;

    static LogBufferModel *s_instance;
    static QtMessageHandler s_previousHandler;

    // Buckets hold enough to serve the largest selectable limit (100).
    static constexpr int kCapacity = 100;
    static constexpr qint64 kRotateBytes = 256 * 1024;
    // ~1 s of a very heavy flood at the 100 ms drain interval. Beyond this the
    // writer can't keep up and further lines are dropped (and counted).
    static constexpr int kMaxPendingWrites = 2048;
};

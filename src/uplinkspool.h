#pragma once

#include <QByteArray>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

// Durable FIFO of telemetry frames that could not be published.
//
// LTE at a track is lossy in a way a desk connection is not: cuttings, pit
// buildings and cell handovers all produce dropouts of seconds to minutes. The
// uplink must therefore be able to fall behind and catch up without losing the
// middle of a session — which is exactly the part a driver wants to look at.
//
// SQLite rather than an in-memory queue because the failure this guards against
// includes losing power: a session ends when the driver switches off, and an
// in-memory backlog would die with it.
//
// Ordering and delivery guarantees, in the order they matter:
//
//  - **FIFO by `id`.** The cloud reconstructs a lap from a contiguous trace;
//    out-of-order delivery would still dedupe correctly but is pointless
//    complexity.
//  - **Rows are deleted on the PUBACK, never on publish.** A power cut between
//    the two therefore re-sends rather than loses. That is safe *only* because
//    the cloud dedupes on `(session_id, seq, time)` — which is also why this
//    class must never try to dedupe on its own. When in doubt, re-send.
//  - **Oldest-first eviction past a row cap.** An unbounded spool on a Pi's SD
//    card is a disk-full crash mid-session. Losing the oldest frames is the
//    right failure mode: recent data is what the driver came back for.
// One spooled message. `topic` is stored rather than assumed, because the spool
// carries session lifecycle events as well as telemetry frames.
//
// That is not an over-generalisation, it is a correctness requirement. A car
// that powers up in a garage with no signal, is driven out, and only finds LTE
// mid-session would otherwise lose its `start` event — and the cloud resolves a
// session from that event, so the entire backlog behind it would arrive
// referencing a session that was never opened. Keeping both kinds in one FIFO
// means `start` is republished ahead of the frames it belongs to, for free.
struct SpooledFrame {
    qint64     id = 0;
    QString    topic;
    QString    sid;
    qint64     seq = 0;
    QByteArray payload;
};

class UplinkSpool : public QObject {
    Q_OBJECT

public:
    // At 10 Hz a session is ~36 000 rows/hour, so this is roughly 14 hours of
    // continuous running — far more than a track day, and still only a few tens
    // of MB of SD card.
    static constexpr int kMaxRows = 500000;

    // One batch in flight at a time. Sized to match the cloud's backfill
    // contract (up to 200 frames per message) and small enough that a marginal
    // LTE link is not asked to move a megabyte before it can acknowledge
    // anything.
    static constexpr int kBatchSize = 200;

    explicit UplinkSpool(QObject *parent = nullptr);
    ~UplinkSpool() override;

    // Path override exists for tests; production always uses
    // AppPaths::dataFile("uplink-spool.sqlite").
    void setDatabasePath(const QString &path);

    // Opened lazily on first use, never in the constructor — main.cpp
    // constructs this during startup and the boot path must not touch the SD
    // card before the first frame is on screen. Safe to call repeatedly.
    bool open();

    // Appends frames in one transaction. A burst of 200 costs one commit, not
    // 200 SD-card syncs.
    bool append(const QVector<SpooledFrame> &frames);
    bool append(const SpooledFrame &frame);

    // Oldest `limit` rows, id-ascending. Does not remove anything — see
    // releaseThrough().
    QVector<SpooledFrame> peek(int limit = kBatchSize);

    // The leading run of rows that share a topic and sid, capped at `limit`.
    //
    // This is what the drain actually uses. A backfill batch carries exactly one
    // `sid` in its payload and goes to exactly one topic, so a batch may not
    // straddle a session boundary or mix a session event in with telemetry —
    // and because the spool is FIFO, taking the leading homogeneous run
    // preserves order while keeping each published message well-formed.
    QVector<SpooledFrame> peekRun(int limit = kBatchSize);

    // Deletes every row with id <= lastId. Called only from the PUBACK path.
    bool releaseThrough(qint64 lastId);

    int  count();
    bool isEmpty() { return count() == 0; }

    // Drops everything. Used when the driver un-pairs the device: a backlog
    // belonging to a car this dash is no longer provisioned for must not be
    // published to whatever it is paired with next.
    bool clear();

private:
    bool ensureSchema();
    // Trims to kMaxRows, oldest first. Called after each append.
    void evictOverflow();

    QString       m_path;
    QSqlDatabase  m_db;
    bool          m_open = false;
    QString       m_connectionName;
};

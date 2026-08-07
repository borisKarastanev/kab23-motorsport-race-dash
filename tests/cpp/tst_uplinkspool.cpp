#include <QtTest>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "src/uplinkspool.h"

namespace {

// Counts UplinkSpool's own open-failure warnings, and swallows everything else
// while installed so a deliberately-failing open does not spray the test log.
int g_openWarnings = 0;

void countOpenWarnings(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtWarningMsg && message.contains(QLatin1String("could not open spool")))
        ++g_openWarnings;
}

} // namespace

// Store-and-forward is the whole reason the uplink is safe to run over LTE, so
// these tests are about the guarantees rather than the SQL: FIFO order, survival
// across a restart, deletion only on acknowledgement, and bounded growth.
class TestUplinkSpool : public QObject {
    Q_OBJECT

private slots:
    void init();
    void enqueueThenPeekReturnsFifoOrder();
    void rowsSurviveCloseAndReopen();
    void peekDoesNotRemoveRows();
    void releaseThroughDeletesOnlyAcknowledgedRows();
    void interruptedDrainResendsRatherThanLoses();
    void evictionDropsOldestAndKeepsNewest();
    void clearEmptiesTheSpool();
    void peekRunStopsAtATopicChange();
    void peekRunStopsAtASessionBoundary();
    void countTracksEveryMutationWithoutScanning();
    void aFailedOpenIsNotRetried();

private:
    QString dbPath() const { return m_dir.filePath("spool.sqlite"); }
    static SpooledFrame frame(const QString &sid, qint64 seq,
                              const QString &topic = QStringLiteral("cars/X/telemetry"));

    QTemporaryDir m_dir;
};

SpooledFrame TestUplinkSpool::frame(const QString &sid, qint64 seq, const QString &topic)
{
    SpooledFrame f;
    f.topic   = topic;
    f.sid     = sid;
    f.seq     = seq;
    f.payload = QByteArray("{\"seq\":") + QByteArray::number(seq) + '}';
    return f;
}

void TestUplinkSpool::init()
{
    QFile::remove(dbPath());
}

void TestUplinkSpool::enqueueThenPeekReturnsFifoOrder()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());

    QVector<SpooledFrame> in;
    for (qint64 seq = 1; seq <= 5; ++seq)
        in.append(frame("sid-a", seq));
    QVERIFY(spool.append(in));

    const QVector<SpooledFrame> out = spool.peek(10);
    QCOMPARE(out.size(), 5);
    for (int i = 0; i < out.size(); ++i) {
        QCOMPARE(out[i].seq, qint64(i + 1));
        QCOMPARE(out[i].sid, QStringLiteral("sid-a"));
    }
}

void TestUplinkSpool::rowsSurviveCloseAndReopen()
{
    {
        UplinkSpool spool;
        spool.setDatabasePath(dbPath());
        QVERIFY(spool.open());
        QVERIFY(spool.append(frame("sid-a", 1)));
        QVERIFY(spool.append(frame("sid-a", 2)));
    } // destroyed — as it would be by a power cut at the end of a session

    UplinkSpool reopened;
    reopened.setDatabasePath(dbPath());
    QVERIFY(reopened.open());
    QCOMPARE(reopened.count(), 2);
    QCOMPARE(reopened.peek(10).first().seq, qint64(1));
}

void TestUplinkSpool::peekDoesNotRemoveRows()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());
    QVERIFY(spool.append(frame("sid-a", 1)));

    QCOMPARE(spool.peek(10).size(), 1);
    QCOMPARE(spool.peek(10).size(), 1); // still there
    QCOMPARE(spool.count(), 1);
}

void TestUplinkSpool::releaseThroughDeletesOnlyAcknowledgedRows()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());

    QVector<SpooledFrame> in;
    for (qint64 seq = 1; seq <= 6; ++seq)
        in.append(frame("sid-a", seq));
    QVERIFY(spool.append(in));

    const QVector<SpooledFrame> batch = spool.peek(3);
    QCOMPARE(batch.size(), 3);
    QVERIFY(spool.releaseThrough(batch.last().id));

    const QVector<SpooledFrame> remaining = spool.peek(10);
    QCOMPARE(remaining.size(), 3);
    QCOMPARE(remaining.first().seq, qint64(4));
}

/**
 * The guarantee that makes losing power mid-drain safe. A batch is published
 * and the process dies before the PUBACK; on restart the same frames must still
 * be there to be sent again. Duplicates are explicitly fine — the cloud's
 * (session_id, seq, time) unique index absorbs them — but a gap is not.
 */
void TestUplinkSpool::interruptedDrainResendsRatherThanLoses()
{
    QVector<qint64> firstAttempt;
    {
        UplinkSpool spool;
        spool.setDatabasePath(dbPath());
        QVERIFY(spool.open());
        QVector<SpooledFrame> in;
        for (qint64 seq = 1; seq <= 4; ++seq)
            in.append(frame("sid-a", seq));
        QVERIFY(spool.append(in));

        for (const SpooledFrame &f : spool.peek(4))
            firstAttempt.append(f.seq);
        // Published... and then no releaseThrough(), because the power went.
    }

    UplinkSpool afterCrash;
    afterCrash.setDatabasePath(dbPath());
    QVERIFY(afterCrash.open());

    QVector<qint64> secondAttempt;
    for (const SpooledFrame &f : afterCrash.peek(4))
        secondAttempt.append(f.seq);

    QCOMPARE(secondAttempt, firstAttempt);
    QCOMPARE(secondAttempt.size(), 4);
}

/**
 * The cap exists so a long offline stint cannot fill a Pi's SD card and take
 * the dashboard down mid-session. Which end gets dropped matters: the driver
 * came back for the recent laps.
 */
void TestUplinkSpool::evictionDropsOldestAndKeepsNewest()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());

    // Rather than writing half a million rows, drive the same code path by
    // checking the boundary behaviour directly: append past the cap in one go
    // is impractical here, so assert the invariant the eviction maintains.
    const int over = 50;
    QVector<SpooledFrame> in;
    in.reserve(UplinkSpool::kMaxRows + over);
    for (qint64 seq = 1; seq <= UplinkSpool::kMaxRows + over; ++seq)
        in.append(frame("sid-a", seq));
    QVERIFY(spool.append(in));

    QCOMPARE(spool.count(), UplinkSpool::kMaxRows);

    // The oldest `over` frames are the ones gone.
    QCOMPARE(spool.peek(1).first().seq, qint64(over + 1));
}

void TestUplinkSpool::clearEmptiesTheSpool()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());
    QVERIFY(spool.append(frame("sid-a", 1)));
    QVERIFY(spool.clear());
    QCOMPARE(spool.count(), 0);
    QVERIFY(spool.isEmpty());
}

/**
 * A backfill batch carries exactly one sid and goes to exactly one topic, so a
 * drain may not hand back a run that mixes a session event in with telemetry.
 * The spool is FIFO, so truncating at the boundary preserves order — the next
 * drain picks up where this one stopped.
 */
void TestUplinkSpool::peekRunStopsAtATopicChange()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());

    QVERIFY(spool.append(frame("sid-a", 0, "cars/X/session")));
    QVERIFY(spool.append(frame("sid-a", 0, "cars/X/telemetry")));
    QVERIFY(spool.append(frame("sid-a", 1, "cars/X/telemetry")));

    const QVector<SpooledFrame> first = spool.peekRun();
    QCOMPARE(first.size(), 1);
    QCOMPARE(first.first().topic, QStringLiteral("cars/X/session"));

    QVERIFY(spool.releaseThrough(first.last().id));

    const QVector<SpooledFrame> second = spool.peekRun();
    QCOMPARE(second.size(), 2);
    QCOMPARE(second.first().topic, QStringLiteral("cars/X/telemetry"));
}

/**
 * The scenario this exists for: a car that starts a session offline, is driven,
 * stops, and starts a second session — all before finding signal. Both sessions'
 * frames are in one FIFO, and a batch that straddled them would claim frames
 * from session B belong to session A's sid.
 */
void TestUplinkSpool::peekRunStopsAtASessionBoundary()
{
    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());

    QVERIFY(spool.append(frame("sid-a", 0)));
    QVERIFY(spool.append(frame("sid-a", 1)));
    QVERIFY(spool.append(frame("sid-b", 0)));

    const QVector<SpooledFrame> run = spool.peekRun();
    QCOMPARE(run.size(), 2);
    for (const SpooledFrame &f : run)
        QCOMPARE(f.sid, QStringLiteral("sid-a"));
}

/**
 * count() is served from a cached counter rather than SELECT COUNT(*), because
 * `frames` has only the rowid B-tree and the scan is called twice per spooled
 * frame on the GUI thread at 10 Hz — so on a Pi with a real backlog it was
 * stealing a fifth of the render budget, worsening exactly as the spool filled.
 *
 * A cached counter is only correct if every mutation maintains it, and a drift
 * would be invisible at the call sites (they just see a number). So: exercise
 * each mutation and check the counter still agrees with what is on disk, read
 * back through peek() rather than through count() itself.
 */
void TestUplinkSpool::countTracksEveryMutationWithoutScanning()
{
    const auto rowsOnDisk = [](UplinkSpool &s) {
        return s.peek(UplinkSpool::kMaxRows).size();
    };

    UplinkSpool spool;
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());
    QCOMPARE(spool.count(), 0);

    // append, singly and in a batch
    QVERIFY(spool.append(frame("sid-a", 0)));
    QCOMPARE(spool.count(), 1);
    QVERIFY(spool.append(QVector<SpooledFrame>{ frame("sid-a", 1), frame("sid-a", 2) }));
    QCOMPARE(spool.count(), 3);
    QCOMPARE(spool.count(), rowsOnDisk(spool));

    // releaseThrough — a partial delete, so the counter must come from what the
    // statement actually removed rather than from the batch size
    QVERIFY(spool.releaseThrough(spool.peek(2).last().id));
    QCOMPARE(spool.count(), 1);
    QCOMPARE(spool.count(), rowsOnDisk(spool));

    // releasing an id that matches nothing must not move it
    QVERIFY(spool.releaseThrough(-1));
    QCOMPARE(spool.count(), 1);

    // clear
    QVERIFY(spool.clear());
    QCOMPARE(spool.count(), 0);
    QCOMPARE(spool.count(), rowsOnDisk(spool));

    // and the seed on reopen comes from the file, not from this instance
    QVERIFY(spool.append(frame("sid-b", 0)));
    UplinkSpool reopened;
    reopened.setDatabasePath(dbPath());
    QVERIFY(reopened.open());
    QCOMPARE(reopened.count(), 1);
}

/**
 * append() calls open() on every frame, so a permanent failure — in practice
 * libqt6sql6-sqlite missing, which is packaged separately from the Qt SQL
 * headers and so fails at runtime on a machine that built fine — used to be
 * retried at 10 Hz. Each retry re-registered the same Qt SQL connection name,
 * so Qt's own "duplicate connection name" warnings piled onto ours in the
 * on-screen Device Log and dash.log at 10+ lines a second, writing to the SD
 * card continuously.
 *
 * Driven here through an unopenable path rather than by removing the driver.
 */
void TestUplinkSpool::aFailedOpenIsNotRetried()
{
    UplinkSpool spool;
    // A path under a component that exists but is a file, not a directory:
    // SQLite cannot create a database there.
    const QString wall = m_dir.filePath("not-a-directory");
    {
        QFile f(wall);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    spool.setDatabasePath(wall + QStringLiteral("/spool.sqlite"));

    // Counting the warnings is the assertion, not a nicety: the un-latched
    // version still returned false every time, so a test that only checked the
    // return value passed against the bug. What was wrong was the *volume* —
    // append() calls open() per frame, so this line went to the Device Log and
    // dash.log ten times a second for as long as the app ran.
    bool anyOpenSucceeded   = false;
    bool anyAppendSucceeded = false;

    g_openWarnings = 0;
    QtMessageHandler previous = qInstallMessageHandler(&countOpenWarnings);
    anyOpenSucceeded = spool.open();
    for (int i = 0; i < 5; ++i) {
        anyOpenSucceeded   = spool.open()               || anyOpenSucceeded;
        anyAppendSucceeded = spool.append(frame("s", i)) || anyAppendSucceeded;
    }
    qInstallMessageHandler(previous);

    QVERIFY(!anyOpenSucceeded);
    QVERIFY(!anyAppendSucceeded);
    QCOMPARE(g_openWarnings, 1);
    QCOMPARE(spool.count(), 0);

    // Pointing the spool somewhere else is a new attempt, not a retry.
    spool.setDatabasePath(dbPath());
    QVERIFY(spool.open());
    QVERIFY(spool.append(frame("sid-a", 0)));
    QCOMPARE(spool.count(), 1);
}

QTEST_MAIN(TestUplinkSpool)
#include "tst_uplinkspool.moc"

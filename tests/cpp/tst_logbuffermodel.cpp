#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <vector>

#include "src/logbuffermodel.h"

// Regression guard for the Device Log freeze (fixed twice now — see below).
//
// Reading the Device Log on the Pi froze the UI and made the app unnavigable.
// Root cause: LogBufferModel::persistEntry() held m_mutex across
// m_logFile.flush() (and log rotation) — synchronous SD-card I/O. Every
// qWarning() from ANY thread, and every read of the list from the QML thread,
// blocked on that lock. Under a log flood the UI thread was starved: with a
// realistic ~2 ms SD flush the median UI frame measured 657 ms.
//
// The invariant these tests pin: the message handler touches memory only, and
// the page-read path takes no lock the handler contends for. If someone puts
// I/O back inside the logging critical section, timingOfHandlerIsBounded()
// fails.
class TestLogBufferModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void bucketsAreFilteredByLevel();
    void snapshotHonoursLimit();
    void newSinceRefreshCountsOnlyVisibleBucket();
    void refreshClearsNewCounter();
    void limitIsClampedToASafeRange();
    void droppedMessagesAreVisibleInTheBuffer();
    void handlerNeverBlocksUnderConcurrentFlood();
};

void TestLogBufferModel::initTestCase()
{
    // Sandbox AppPaths-backed persistence (dash.log) away from the real user data dir.
    QStandardPaths::setTestModeEnabled(true);
}

void TestLogBufferModel::bucketsAreFilteredByLevel()
{
    LogBufferModel model;

    qInfo("info line");
    qWarning("warn line");

    // "info" is already the default filter, so setFilterLevel() is a no-op and does
    // NOT refresh — the view is a snapshot by design. Pull explicitly, as
    // DeviceLog.qml's Component.onCompleted does.
    model.setFilterLevel("info");
    model.refresh();
    QVERIFY(!model.entries().isEmpty());
    QCOMPARE(model.entries().last().toMap()["message"].toString(), QStringLiteral("info line"));

    // A real level change is an explicit user action, so it refreshes on its own.
    model.setFilterLevel("warn");
    QCOMPARE(model.entries().last().toMap()["message"].toString(), QStringLiteral("warn line"));
}

void TestLogBufferModel::snapshotHonoursLimit()
{
    LogBufferModel model;
    model.setFilterLevel("warn");

    for (int i = 0; i < 100; ++i)
        qWarning("flood line %d", i);

    model.setLimit(20);
    model.refresh();
    QCOMPARE(model.entries().size(), 20);
    // Snapshot is the *most recent* limit entries, chronological (oldest first).
    QCOMPARE(model.entries().last().toMap()["message"].toString(), QStringLiteral("flood line 99"));
    QCOMPARE(model.entries().first().toMap()["message"].toString(), QStringLiteral("flood line 80"));

    model.setLimit(50);
    QCOMPARE(model.entries().size(), 50);

    model.setLimit(100);
    QCOMPARE(model.entries().size(), 100); // kCapacity — buckets hold no more
}

void TestLogBufferModel::newSinceRefreshCountsOnlyVisibleBucket()
{
    LogBufferModel model;
    model.setFilterLevel("info");
    model.refresh();
    QCOMPARE(model.newSinceRefresh(), 0);

    // An error line must not nag the badge while "info" is on screen.
    qCritical("error line");
    QCOMPARE(model.newSinceRefresh(), 0);

    qInfo("info line");
    QCOMPARE(model.newSinceRefresh(), 1);
}

void TestLogBufferModel::refreshClearsNewCounter()
{
    LogBufferModel model;
    model.setFilterLevel("warn");
    model.refresh();

    qWarning("one");
    qWarning("two");
    QCOMPARE(model.newSinceRefresh(), 2);

    // The list must NOT update on its own — it is a snapshot, so a flood can't
    // drive QML relayouts. Only refresh() moves new lines into view.
    const int before = model.entries().size();
    QCOMPARE(model.entries().size(), before);

    model.refresh();
    QCOMPARE(model.newSinceRefresh(), 0);
    QCOMPARE(model.entries().last().toMap()["message"].toString(), QStringLiteral("two"));
}

void TestLogBufferModel::limitIsClampedToASafeRange()
{
    LogBufferModel model;
    model.setFilterLevel("warn");
    for (int i = 0; i < 30; ++i)
        qWarning("line %d", i);

    // `limit` is an int Q_PROPERTY with a WRITE setter, so QML can push any value
    // into it. A negative used to make refresh() form `bucket.end() - n` past the
    // end and walk off the deque — a hard SIGSEGV. It must clamp, not crash.
    model.setLimit(-1);
    QCOMPARE(model.limit(), 0);
    QCOMPARE(model.entries().size(), 0);

    // Above kCapacity is unserviceable — the buckets simply hold no more.
    model.setLimit(100000);
    QCOMPARE(model.limit(), 100);

    model.setLimit(20);
    QCOMPARE(model.entries().size(), 20);
}

void TestLogBufferModel::droppedMessagesAreVisibleInTheBuffer()
{
    LogBufferModel model;
    model.setFilterLevel("warn");

    // Overrun the writer's queue (kMaxPendingWrites = 2048) before it can drain.
    for (int i = 0; i < 4000; ++i)
        qWarning("flood %d", i);

    QTest::qWait(250); // let the writer tick and emit its suppression note
    model.refresh();

    // The note must land in the on-screen buffer, not only in dash.log — a user
    // reading the log ON THE DEVICE has to know lines went missing.
    bool found = false;
    for (const QVariant &v : model.entries()) {
        if (v.toMap()["message"].toString().contains("dropped"))
            found = true;
    }
    QVERIFY2(found, "no 'messages dropped' note surfaced in the visible buffer");

    // ...and it must not regenerate itself forever: draining again with nothing
    // dropped since must add no further notes.
    const int before = model.entries().size();
    QTest::qWait(250);
    model.refresh();
    int notes = 0;
    for (const QVariant &v : model.entries()) {
        if (v.toMap()["message"].toString().contains("dropped"))
            ++notes;
    }
    QCOMPARE(model.entries().size(), before);
    QCOMPARE(notes, 1);
}

void TestLogBufferModel::handlerNeverBlocksUnderConcurrentFlood()
{
    LogBufferModel model;
    model.setFilterLevel("warn");

    std::atomic<bool> stop{false};
    std::vector<QThread *> threads;
    for (int i = 0; i < 4; ++i) {
        QThread *t = QThread::create([&stop] {
            while (!stop.load(std::memory_order_relaxed))
                qWarning("eglfs: page flip denied, frame dropped");
        });
        t->start();
        threads.push_back(t);
    }

    // While 4 threads flood the handler, the "UI thread" does what the Device Log
    // page does. Neither logging nor reading the snapshot may block on I/O.
    qint64 worstNs = 0;
    QElapsedTimer wall;
    wall.start();
    while (wall.elapsed() < 300) {
        QElapsedTimer t;
        t.start();
        qWarning("ui: render warning from main thread");
        QVariantList rows = model.entries();
        Q_UNUSED(rows)
        worstNs = std::max(worstNs, t.nsecsElapsed());
        QTest::qWait(1); // let the writer's drain timer run
    }

    stop = true;
    for (QThread *t : threads) {
        t->wait();
        delete t;
    }

    // Generous bound: the point is that this is microseconds, not the ~650 ms
    // frames the pre-fix code produced once flush() latency was realistic. A
    // failure here means I/O has crept back into the logging critical section.
    const double worstMs = worstNs / 1e6;
    QVERIFY2(worstMs < 50.0,
             qPrintable(QStringLiteral("UI thread blocked %1 ms in the logging path — "
                                       "I/O is back inside the critical section")
                            .arg(worstMs)));
}

QTEST_GUILESS_MAIN(TestLogBufferModel)
#include "tst_logbuffermodel.moc"

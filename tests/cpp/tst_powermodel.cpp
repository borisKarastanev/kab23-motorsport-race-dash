#include <QTest>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QTimer>

#include "src/can/candatamodel.h"
#include "src/can/canframes.h"
#include "src/device/powermodel.h"

// Short timings so the whole idle → countdown → poweroff path runs in well
// under a second. Always mock mode: never actually power anything off.
static constexpr int kIdleMs    = 200;
static constexpr int kCountdown = 3;
static constexpr int kLongCountdown = 100; // can't run out under a test
static constexpr int kTickMs    = 50;
static constexpr int kWaitMs    = 2000;

// A stopped engine (one 0-rpm frame, then a quiet bus), with `window`
// standing in for the QQuickWindow PowerModel watches in production.
// feedCan = false: no frame at all, like a missing adapter at boot.
struct Rig {
    CanDataModel can;
    QObject window;
    PowerModel power{&can, true};

    explicit Rig(int countdown = kCountdown, bool feedCan = true)
    {
        power.setInputSource(&window);
        power.setTimings(kIdleMs, countdown, kTickMs);
        if (feedCan) {
            can.onFrame(CanFrames::rpm(0));
            waitArmed(); // canSeen lands on CanDataModel's next notify tick
        }
    }
    bool waitArmed()        { return QTest::qWaitFor([this] { return power.armed(); }, kWaitMs); }
    bool waitPending()      { return QTest::qWaitFor([this] { return power.shutdownPending(); }, kWaitMs); }
    bool waitShuttingDown() { return QTest::qWaitFor([this] { return power.shuttingDown(); }, kWaitMs); }
};

// Keeps a running engine on the bus (10 Hz, like the live DME frames) for as
// long as it's alive.
class EngineFeed {
public:
    explicit EngineFeed(CanDataModel &model)
    {
        QObject::connect(&m_timer, &QTimer::timeout, [&model] {
            model.onFrame(CanFrames::rpm(900));
            model.onFrame(CanFrames::dme2(80.0, 0.0, true));
        });
        m_timer.start(100);
    }
private:
    QTimer m_timer;
};

static void press(QObject &target)
{
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&target, &ev);
}

static void keyPress(QObject &target)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(&target, &ev);
}

class TestPowerModel : public QObject {
    Q_OBJECT

private slots:
    void armedWithEngineStoppedAndStationary();
    void countdownAfterIdleThenPowersOff();
    void interactionRestartsIdleWait();
    void inputDuringCountdownLeftToOverlay();
    void inputElsewhereIgnored();
    void engineStartDisarmsAndCancels();
    void movingCarNeverArms();
    void silentBusNeverArms();
    void gpsDropoutKeepsMovingHold();
    void blockedDisarms();
    void mockPoweroffReturnsToIdleWatch();
    void lastMomentChangeAbortsWithoutFlushing();
};

void TestPowerModel::armedWithEngineStoppedAndStationary()
{
    Rig rig;
    QVERIFY(rig.waitArmed());
    QVERIFY(!rig.power.shutdownPending());
}

void TestPowerModel::countdownAfterIdleThenPowersOff()
{
    Rig rig;
    QSignalSpy poweroff(&rig.power, &PowerModel::aboutToPowerOff);

    QVERIFY(rig.waitPending());
    QCOMPARE(rig.power.secondsRemaining(), kCountdown);

    QVERIFY(rig.waitShuttingDown());
    QVERIFY(!rig.power.shutdownPending());
    QVERIFY(QTest::qWaitFor([&] { return poweroff.count() == 1; }, kWaitMs));
}

void TestPowerModel::interactionRestartsIdleWait()
{
    Rig rig;

    // Keep touching (or pressing keys) more often than the idle timeout: the
    // countdown never shows.
    for (int i = 0; i < 6; ++i) {
        QTest::qWait(kIdleMs / 2);
        if (i % 2)
            keyPress(rig.window);
        else
            press(rig.window);
        QVERIFY(!rig.power.shutdownPending());
    }
    // Then leave it alone.
    QVERIFY(rig.waitPending());
}

void TestPowerModel::inputDuringCountdownLeftToOverlay()
{
    Rig rig(kLongCountdown);
    QSignalSpy poweroff(&rig.power, &PowerModel::aboutToPowerOff);

    QVERIFY(rig.waitPending());
    // The filter must not hide the overlay ahead of the window's own delivery,
    // or the tap would fall through to the button underneath.
    press(rig.window);
    keyPress(rig.window);
    QVERIFY(rig.power.shutdownPending());

    // The overlay's MouseArea / Keys handler then cancels.
    rig.power.cancelShutdown();
    QVERIFY(!rig.power.shutdownPending());
    QVERIFY(rig.power.armed());

    // The idle wait starts over and the countdown comes back from the top.
    QVERIFY(rig.waitPending());
    QCOMPARE(rig.power.secondsRemaining(), kLongCountdown);
    QCOMPARE(poweroff.count(), 0);
}

void TestPowerModel::inputElsewhereIgnored()
{
    Rig rig;
    QObject other;

    // Only the watched window counts as interaction.
    for (int i = 0; i < 6 && !rig.power.shutdownPending(); ++i) {
        QTest::qWait(kIdleMs / 2);
        press(other);
    }
    QVERIFY(rig.waitPending());
}

void TestPowerModel::engineStartDisarmsAndCancels()
{
    Rig rig(kLongCountdown);
    QVERIFY(rig.waitPending());

    EngineFeed feed(rig.can);
    QVERIFY(QTest::qWaitFor([&] { return !rig.power.armed(); }, kWaitMs));
    QVERIFY(!rig.power.shutdownPending());

    // Stays disarmed for as long as the engine runs.
    QTest::qWait(kIdleMs * 2);
    QVERIFY(!rig.power.shutdownPending());
}

void TestPowerModel::movingCarNeverArms()
{
    Rig rig;

    // No engine frames (a dead CAN adapter mid-session) but GPS says moving.
    rig.power.setGpsFix(true);
    rig.can.setSpeed(60);
    QVERIFY(QTest::qWaitFor([&] { return !rig.power.armed(); }, kWaitMs));
    QTest::qWait(kIdleMs * 2);
    QVERIFY(!rig.power.shutdownPending());

    rig.can.setSpeed(0); // GPS with a fix vouches for the standstill
    QVERIFY(rig.waitArmed());
}

void TestPowerModel::silentBusNeverArms()
{
    // No CAN frame yet (adapter missing or slow at boot): "engine stopped"
    // is only silence, so nothing arms however long it stays quiet.
    Rig rig(kCountdown, /*feedCan=*/false);
    QTest::qWait(kIdleMs * 2);
    QVERIFY(!rig.power.armed());
    QVERIFY(!rig.power.shutdownPending());

    rig.can.onFrame(CanFrames::rpm(0));
    QVERIFY(rig.waitArmed());
}

void TestPowerModel::gpsDropoutKeepsMovingHold()
{
    Rig rig;
    rig.can.setSpeed(60);
    QVERIFY(QTest::qWaitFor([&] { return !rig.power.armed(); }, kWaitMs));

    // No fix: a GPS dropout (or no RaceBox) reports 0, which proves nothing.
    rig.can.setSpeed(0);
    QTest::qWait(kIdleMs * 2);
    QVERIFY(!rig.power.armed());
    QVERIFY(!rig.power.shutdownPending());

    rig.power.setGpsFix(true); // the fix comes back, still at 0 km/h
    QVERIFY(rig.waitArmed());
}

void TestPowerModel::blockedDisarms()
{
    Rig rig(kLongCountdown);
    QVERIFY(rig.waitPending());

    rig.power.setBlocked(true); // update install started
    QVERIFY(!rig.power.armed());
    QVERIFY(!rig.power.shutdownPending());

    rig.power.setBlocked(false);
    QVERIFY(rig.power.armed());
}

void TestPowerModel::mockPoweroffReturnsToIdleWatch()
{
    Rig rig;
    QSignalSpy aborted(&rig.power, &PowerModel::powerOffAborted);
    QVERIFY(rig.waitShuttingDown());

    // Mock mode doesn't power off; it drops back to watching for idle, and
    // tells the flushed models to come back up.
    QVERIFY(QTest::qWaitFor([&] { return !rig.power.shuttingDown(); }, 4000));
    QVERIFY(rig.power.armed());
    QCOMPARE(aborted.count(), 1);
}

void TestPowerModel::lastMomentChangeAbortsWithoutFlushing()
{
    Rig rig;
    QSignalSpy poweroff(&rig.power, &PowerModel::aboutToPowerOff);
    QSignalSpy aborted(&rig.power, &PowerModel::powerOffAborted);
    QVERIFY(rig.waitShuttingDown());

    // Something changes between the countdown running out and the poweroff
    // (same re-check as an engine restart or the car moving off).
    rig.power.setBlocked(true);
    QVERIFY(QTest::qWaitFor([&] { return !rig.power.shuttingDown(); }, kWaitMs));
    QCOMPARE(poweroff.count(), 0);
    QCOMPARE(aborted.count(), 0); // nothing was flushed, nothing to bring back
    QVERIFY(!rig.power.armed());
}

QTEST_MAIN(TestPowerModel)
#include "tst_powermodel.moc"

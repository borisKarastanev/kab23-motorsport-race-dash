#include <QTest>
#include <QSignalSpy>
#include <cmath>

#include "src/raceboxmodel.h"
#include "src/raceboxdata.h"

namespace {

// A short east-west gate straddling latitude 51.0, centred on longitude 0.
constexpr double kGateLat  = 51.0;
constexpr double kGateLonA = -0.001;
constexpr double kGateLonB =  0.001;

RaceBoxData makeFix(double lat, double lon, bool hasFix = true, int speedMmS = 16667 /* ~60 km/h */)
{
    RaceBoxData d{};
    d.fixStatus  = hasFix ? 3 : 0;
    d.fixFlags   = hasFix ? 0x01 : 0x00;
    d.numSvs     = 8;
    d.latitude   = lat;
    d.longitude  = lon;
    d.speedMmS   = speedMmS;
    d.gForceXMg  = 0;
    d.gForceYMg  = 0;
    d.gForceZMg  = 0;
    d.batteryRaw = 0;
    return d;
}

}

class TestRaceBoxModel : public QObject {
    Q_OBJECT

private slots:
    void haversineKnownDistance();
    void haversineZeroForSamePoint();
    void lapTimerStateTransitionsIdleArmed();
    void crossingGateArmsLap();
    void reverseCrossingRejected();
    void secondSameDirectionCrossingCompletesLap();
    void batteryAndGForceDecodeIndependentOfFix();
    void cannotLearnFinishLineWithoutFix();
    void learnFinishLineNoOpWithoutHeading();
    void learnFinishLineSucceedsOnceMoving();
    void headingRefreshesAtLowSpeed();
    void staleHeadingBlocksLearningAndExpiresLive();
    void canLearnFinishLineChangedFiresOnTransitionOnly();
};

void TestRaceBoxModel::haversineKnownDistance()
{
    // 1 degree of longitude at the equator: R * dLon(rad), R=6371000.
    // 6371000 * (pi/180) ~= 111194.9 m.
    const double d = RaceBoxModel::haversineM(0.0, 0.0, 0.0, 1.0);
    QVERIFY2(std::abs(d - 111194.9) < 50.0, qPrintable(QString::number(d)));
}

void TestRaceBoxModel::haversineZeroForSamePoint()
{
    QCOMPARE(RaceBoxModel::haversineM(51.5, -0.1, 51.5, -0.1), 0.0);
}

void TestRaceBoxModel::lapTimerStateTransitionsIdleArmed()
{
    RaceBoxModel model;
    QCOMPARE(model.lapTimerState(), RaceBoxModel::Idle);

    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    QCOMPARE(model.lapTimerState(), RaceBoxModel::Idle); // finish line set, but no fix yet

    model.onData(makeFix(50.999, 0.0));
    QCOMPARE(model.lapTimerState(), RaceBoxModel::Armed); // fix + gate, never crossed
}

void TestRaceBoxModel::crossingGateArmsLap()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    model.onData(makeFix(50.9998, 0.0)); // south of the gate
    QTest::qWait(40);
    model.onData(makeFix(51.0005, 0.0)); // north of the gate -> crosses

    QCOMPARE(model.lapNumber(), 1);
    QCOMPARE(model.lapTimerState(), RaceBoxModel::Running);
    QVERIFY(model.currentLapMs() >= 0);
}

void TestRaceBoxModel::reverseCrossingRejected()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    model.onData(makeFix(50.9998, 0.0)); // south
    QTest::qWait(40);
    model.onData(makeFix(51.0005, 0.0)); // north-bound crossing -> arms, latches direction
    QCOMPARE(model.lapNumber(), 1);

    QTest::qWait(40);
    model.onData(makeFix(50.9998, 0.0)); // south-bound (reverse) crossing -> must be rejected

    QCOMPARE(model.lapNumber(), 1); // unchanged
}

void TestRaceBoxModel::secondSameDirectionCrossingCompletesLap()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    model.onData(makeFix(50.9990, 0.0));
    QTest::qWait(40);
    model.onData(makeFix(51.0006, 0.0)); // arms lap 1 (north-bound)
    QCOMPARE(model.lapNumber(), 1);

    // The lap-completion debounce (kMinLapMs = 3000) compares against a real
    // QElapsedTimer with no test seam, so completing a lap genuinely requires
    // waiting out real wall-clock time here.
    QTest::qWait(3100);

    model.onData(makeFix(50.9990, 0.0)); // reposition south; segment gap > 500ms, no crossing test
    QTest::qWait(40);
    model.onData(makeFix(51.0006, 0.0)); // north-bound again -> completes lap 1, starts lap 2

    QCOMPARE(model.lapNumber(), 2);
    QVERIFY(model.lastLapMs() > 3000);
    QCOMPARE(model.bestLapMs(), model.lastLapMs()); // first completed lap is the best by definition
    QCOMPARE(model.lapTimerState(), RaceBoxModel::Running);
}

void TestRaceBoxModel::batteryAndGForceDecodeIndependentOfFix()
{
    RaceBoxModel model;
    QSignalSpy speedSpy(&model, &RaceBoxModel::speedKmhChanged);

    RaceBoxData d = makeFix(0.0, 0.0, /*hasFix=*/false);
    d.batteryRaw = 0x55; // 0b01010101 -> 85%, not charging
    d.gForceXMg  = 500;  // 0.5 g
    model.onData(d);

    QCOMPARE(model.batteryPercent(), 85);
    QCOMPARE(model.batteryCharging(), false);
    QCOMPARE(model.gForceX(), 0.5);
    QCOMPARE(speedSpy.count(), 1);
    QCOMPARE(speedSpy.takeFirst().at(0).toInt(), 60);

    RaceBoxData d2 = makeFix(0.0, 0.0, /*hasFix=*/false);
    d2.batteryRaw = 0xD2; // 0b11010010 -> 0x52=82%, charging bit set
    model.onData(d2);

    QCOMPARE(model.batteryPercent(), 82);
    QCOMPARE(model.batteryCharging(), true);
}

void TestRaceBoxModel::cannotLearnFinishLineWithoutFix()
{
    RaceBoxModel model;
    QCOMPARE(model.canLearnFinishLine(), false); // no fix, no heading

    // Movement without a fix must not make the model claim it can learn a gate.
    model.onData(makeFix(51.00000, 0.0, /*hasFix=*/false));
    model.onData(makeFix(51.00010, 0.0, /*hasFix=*/false));
    QCOMPARE(model.canLearnFinishLine(), false);

    model.learnFinishLineHere();
    QCOMPARE(model.finishLineSet(), false);
}

void TestRaceBoxModel::learnFinishLineNoOpWithoutHeading()
{
    RaceBoxModel model;

    // Repeated fixes at the same point (parked, GPS jitter aside): no travel
    // heading is ever established, so learning a gate must be a no-op —
    // regression guard for the button silently doing nothing while stationary.
    model.onData(makeFix(51.0, 0.0));
    model.onData(makeFix(51.0, 0.0));
    QCOMPARE(model.canLearnFinishLine(), false);

    model.learnFinishLineHere();
    QCOMPARE(model.finishLineSet(), false);
}

void TestRaceBoxModel::learnFinishLineSucceedsOnceMoving()
{
    RaceBoxModel model;

    model.onData(makeFix(51.00000, 0.0));
    model.onData(makeFix(51.00010, 0.0)); // ~11 m north -> establishes heading
    QCOMPARE(model.canLearnFinishLine(), true);

    model.learnFinishLineHere();
    QCOMPARE(model.finishLineSet(), true);
}

void TestRaceBoxModel::headingRefreshesAtLowSpeed()
{
    RaceBoxModel model;

    // ~0.1 m per fix — a crawl. Differencing consecutive fixes against a 0.5 m
    // threshold would never yield a heading at this speed (at the device's 25 Hz
    // that threshold implies ~45 km/h); anchoring accumulates the movement instead.
    constexpr double kStepDeg = 9e-7; // ~0.1 m of latitude
    for (int i = 0; i < 10; ++i)
        model.onData(makeFix(51.0 + i * kStepDeg, 0.0, true, /*speedMmS=*/2778 /* ~10 km/h */));

    QVERIFY(model.canLearnFinishLine());
    model.learnFinishLineHere();
    QCOMPARE(model.finishLineSet(), true);
}

void TestRaceBoxModel::staleHeadingBlocksLearningAndExpiresLive()
{
    RaceBoxModel model;
    QSignalSpy spy(&model, &RaceBoxModel::canLearnFinishLineChanged);

    model.onData(makeFix(51.00000, 0.0));
    model.onData(makeFix(51.00010, 0.0)); // moving -> heading established
    QVERIFY(model.canLearnFinishLine());

    // Park. The car keeps a fix but stops moving, so the heading stops being
    // refreshed and ages out — the direction it was last travelling says nothing
    // about how it will next cross the line. Learning must be refused, and the
    // property must flip live (no frame drives this; only the passage of time).
    QTest::qWait(5200); // > kHeadingMaxAgeMs (5000)

    QCOMPARE(model.canLearnFinishLine(), false);
    QCOMPARE(spy.count(), 2); // once true on movement, once false on expiry

    model.learnFinishLineHere();
    QCOMPARE(model.finishLineSet(), false);

    // Driving again re-establishes it.
    model.onData(makeFix(51.00020, 0.0));
    model.onData(makeFix(51.00030, 0.0));
    QVERIFY(model.canLearnFinishLine());
    model.learnFinishLineHere();
    QCOMPARE(model.finishLineSet(), true);
}

void TestRaceBoxModel::canLearnFinishLineChangedFiresOnTransitionOnly()
{
    RaceBoxModel model;
    QSignalSpy spy(&model, &RaceBoxModel::canLearnFinishLineChanged);

    model.onData(makeFix(51.00000, 0.0));
    model.onData(makeFix(51.00010, 0.0)); // fix + heading -> becomes learnable
    QTest::qWait(150);                    // let the 10 Hz notify timer flush
    QCOMPARE(spy.count(), 1);

    model.onData(makeFix(51.00020, 0.0)); // still learnable -> must not re-fire
    QTest::qWait(150);
    QCOMPARE(spy.count(), 1);

    // Losing the fix flips the capability back off — the half a heading-only
    // dirty bit would have missed.
    model.onData(makeFix(51.00020, 0.0, /*hasFix=*/false));
    QTest::qWait(150);
    QCOMPARE(model.canLearnFinishLine(), false);
    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(TestRaceBoxModel)
#include "tst_raceboxmodel.moc"

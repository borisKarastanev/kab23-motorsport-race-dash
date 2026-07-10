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

QTEST_GUILESS_MAIN(TestRaceBoxModel)
#include "tst_raceboxmodel.moc"

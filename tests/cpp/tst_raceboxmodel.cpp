#include <QTest>
#include <QSignalSpy>
#include <cmath>

#include "src/race/raceboxmodel.h"
#include "src/race/raceboxdata.h"

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

// Feeds a straight-line run of fixes from (fromLat,fromLon) to (toLat,toLon)
// (exclusive of the start — the caller must already have fed that point),
// waiting stepWaitMs of real time after each so consecutive fixes stay well
// under kMaxFixGapMs (500 ms) while accumulating real elapsed time for the
// lap-completion debounce (kMinLapMs, 3000 ms).
void driveTo(RaceBoxModel &model, double fromLat, double fromLon, double toLat, double toLon,
            int steps, int stepWaitMs)
{
    for (int i = 1; i <= steps; ++i) {
        const double f = static_cast<double>(i) / steps;
        model.onData(makeFix(fromLat + f * (toLat - fromLat), fromLon + f * (toLon - fromLon)));
        QTest::qWait(stepWaitMs);
    }
}

// A small rectangular circuit whose right-hand edge (SE -> NE) runs due north
// through the finish gate at (kGateLat, ~0.0) — the same crossing geometry
// crossingGateArmsLap() already uses, just continued on around a full loop
// instead of stopping at the first crossing. Reused, driven repeatedly, by
// the sector-gate tests below: since every lap retraces the exact same
// points, whichever gates get derived from the first completed lap are
// guaranteed to be re-crossed by every lap after it.
constexpr double kLoopNELat = 51.0020, kLoopNELon =  0.0000;
constexpr double kLoopNWLat = 51.0020, kLoopNWLon = -0.0030;
constexpr double kLoopSWLat = 50.9980, kLoopSWLon = -0.0030;
constexpr double kLoopSELat = 50.9980, kLoopSELon =  0.0000;
// Just north of the gate — both the arming crossing point and where the car
// sits at the start/end of every lap around the loop.
constexpr double kLoopCrossLat = 51.0006, kLoopCrossLon = 0.0000;

// Drives one full lap around the loop from the current position (which must
// be kLoopCrossLat/Lon, i.e. just north of the gate — true both right after
// arming and after every prior full lap driven by this function), completing
// it by crossing the gate again. ~4 s of real wall time.
void driveFullLoopLap(RaceBoxModel &model)
{
    driveTo(model, kLoopCrossLat, kLoopCrossLon, kLoopNELat, kLoopNELon, 4, 200);
    driveTo(model, kLoopNELat, kLoopNELon, kLoopNWLat, kLoopNWLon, 4, 200);
    driveTo(model, kLoopNWLat, kLoopNWLon, kLoopSWLat, kLoopSWLon, 4, 200);
    driveTo(model, kLoopSWLat, kLoopSWLon, kLoopSELat, kLoopSELon, 4, 200);
    driveTo(model, kLoopSELat, kLoopSELon, kLoopCrossLat, kLoopCrossLon, 4, 200);
}

// Drives a "lap" that cuts straight across the middle of the loop — from the
// start/end point directly to SE and back — never going anywhere near the
// NE/NW/SW corners where the derived sector gates live. Same ~4 s of real
// wall time as a full lap, so the completion debounce still clears, but the
// interior gate(s) are missed.
void driveShortcutLap(RaceBoxModel &model)
{
    driveTo(model, kLoopCrossLat, kLoopCrossLon, kLoopSELat, kLoopSELon, 10, 200);
    driveTo(model, kLoopSELat, kLoopSELon, kLoopCrossLat, kLoopCrossLon, 10, 200);
}

// Derives the loop's real sector gates the way a first completed lap does, and
// hands them back in the persisted map shape — the stand-in for "gates restored
// for a track already timed on a previous run".
QVariantList captureLoopGates()
{
    QVariantList captured;
    RaceBoxModel seed;
    QObject::connect(&seed, &RaceBoxModel::sectorGatesLearned,
                     [&](const QVariantList &g) { captured = g; });
    seed.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    seed.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    seed.onData(makeFix(kLoopCrossLat, kLoopCrossLon));
    driveFullLoopLap(seed);
    return captured;
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
    void lowSpeedNoiseFlooredToZero();
    void speedAboveNoiseFloorReportedExactly();
    void cannotLearnFinishLineWithoutFix();
    void learnFinishLineNoOpWithoutHeading();
    void learnFinishLineSucceedsOnceMoving();
    void headingRefreshesAtLowSpeed();
    void staleHeadingBlocksLearningAndExpiresLive();
    void canLearnFinishLineChangedFiresOnTransitionOnly();
    void sectorGatesDerivedInterpolatedAndReusedAcrossLaps();
    void missedSectorGateYieldsNoSplits();
    void setSectorGatesAppliesImmediatelyWithoutWastingALap();
    void learnFinishLineHereClearsExistingSectorGates();
    void learnFinishLineHereAnnouncesTheClearEvenWithNoGatesLive();
    void wrongWaySectorGateCrossingIgnored();
    void setSectorGatesDiscardsTheCurrentLapsSplits();
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

void TestRaceBoxModel::lowSpeedNoiseFlooredToZero()
{
    RaceBoxModel model;
    QSignalSpy speedSpy(&model, &RaceBoxModel::speedKmhChanged);

    // Regression guard: without a satellite lock the reported GPS speed jitters
    // between ~0-2 km/h while genuinely parked. Anything below the noise floor
    // must be reported as a flat 0, not flicker the dashboard.
    model.onData(makeFix(0.0, 0.0, /*hasFix=*/false, /*speedMmS=*/278));  // ~1 km/h
    QCOMPARE(model.speedKmh(), 0);
    QCOMPARE(speedSpy.count(), 0); // starts at 0; flooring to 0 must not emit a change

    model.onData(makeFix(0.0, 0.0, /*hasFix=*/false, /*speedMmS=*/556));  // ~2 km/h
    QCOMPARE(model.speedKmh(), 0);
    QCOMPARE(speedSpy.count(), 0);
}

void TestRaceBoxModel::speedAboveNoiseFloorReportedExactly()
{
    RaceBoxModel model;
    QSignalSpy speedSpy(&model, &RaceBoxModel::speedKmhChanged);

    model.onData(makeFix(0.0, 0.0, /*hasFix=*/false, /*speedMmS=*/834)); // ~3 km/h, at the floor
    QCOMPARE(model.speedKmh(), 3);
    QCOMPARE(speedSpy.count(), 1);
    QCOMPARE(speedSpy.takeFirst().at(0).toInt(), 3);

    model.onData(makeFix(0.0, 0.0, /*hasFix=*/false, /*speedMmS=*/16667)); // ~60 km/h
    QCOMPARE(model.speedKmh(), 60);
    QCOMPARE(speedSpy.count(), 1);
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

void TestRaceBoxModel::sectorGatesDerivedInterpolatedAndReusedAcrossLaps()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    QList<QList<qint64>> received;
    connect(&model, &RaceBoxModel::lapCompleted,
            [&](const RaceBoxLapResult &lap) { received.append(lap.sectorMs); });

    // Arm: south of the gate, then north -> crosses, lands at kLoopCrossLat/Lon.
    model.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    model.onData(makeFix(kLoopCrossLat, kLoopCrossLon));
    QCOMPARE(model.lapNumber(), 1);

    // Lap 1: no sector gates exist yet, so it can only ever derive them for
    // everyone after it — it cannot report splits for itself.
    driveFullLoopLap(model);
    QCOMPARE(model.lapNumber(), 2);
    QCOMPARE(received.size(), 1);
    QVERIFY(received[0].isEmpty());

    // Lap 2: the gates derived from lap 1 now get crossed for real, with
    // interpolated sub-sample timing — three positive splits that sum to
    // exactly the lap time (no time lost or invented at the seams).
    driveFullLoopLap(model);
    QCOMPARE(model.lapNumber(), 3);
    QCOMPARE(received.size(), 2);
    const QList<qint64> lap2Sectors = received[1];
    QCOMPARE(lap2Sectors.size(), 3);
    for (qint64 s : lap2Sectors) QVERIFY(s > 0);
    QCOMPARE(lap2Sectors[0] + lap2Sectors[1] + lap2Sectors[2], model.lastLapMs());

    // Lap 3: same fixed gates, driven again — boundaries are identical across
    // laps, not re-derived, so this keeps working rather than degrading.
    driveFullLoopLap(model);
    QCOMPARE(model.lapNumber(), 4);
    QCOMPARE(received.size(), 3);
    const QList<qint64> lap3Sectors = received[2];
    QCOMPARE(lap3Sectors.size(), 3);
    for (qint64 s : lap3Sectors) QVERIFY(s > 0);
    QCOMPARE(lap3Sectors[0] + lap3Sectors[1] + lap3Sectors[2], model.lastLapMs());
}

void TestRaceBoxModel::missedSectorGateYieldsNoSplits()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    QList<QList<qint64>> received;
    connect(&model, &RaceBoxModel::lapCompleted,
            [&](const RaceBoxLapResult &lap) { received.append(lap.sectorMs); });

    model.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    model.onData(makeFix(kLoopCrossLat, kLoopCrossLon));

    driveFullLoopLap(model); // lap 1 — derives the gates, reports nothing itself
    driveFullLoopLap(model); // lap 2 — both gates crossed normally
    QCOMPARE(received.size(), 2);
    QCOMPARE(received[1].size(), 3);

    // Lap 3 cuts straight across the middle of the loop, nowhere near the
    // NE/NW/SW corners the derived gates sit at/along — a lap that misses a
    // gate reports no splits at all, never a partial or guessed list.
    driveShortcutLap(model);
    QCOMPARE(model.lapNumber(), 4);
    QCOMPARE(received.size(), 3);
    QVERIFY(received[2].isEmpty());
}

void TestRaceBoxModel::setSectorGatesAppliesImmediatelyWithoutWastingALap()
{
    // First, learn what the real derived gates for this loop look like — a
    // stand-in for "gates persisted from a track already timed on a previous
    // run" (see TrackModel::onSectorGatesLearned).
    const QVariantList capturedGates = captureLoopGates();
    QCOMPARE(capturedGates.size(), 2);

    // A fresh model — standing in for a fresh app run — restores the
    // persisted gates before driving a single fix.
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    model.setSectorGates(capturedGates);

    QList<QList<qint64>> received;
    connect(&model, &RaceBoxModel::lapCompleted,
            [&](const RaceBoxLapResult &lap) { received.append(lap.sectorMs); });

    model.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    model.onData(makeFix(kLoopCrossLat, kLoopCrossLon));
    driveFullLoopLap(model); // this model's very FIRST lap — no derivation needed

    QCOMPARE(received.size(), 1);
    QCOMPARE(received[0].size(), 3); // splits reported immediately, not "no gates yet"
}

void TestRaceBoxModel::learnFinishLineHereClearsExistingSectorGates()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    QVariantMap gate;
    gate["lat1"] = 51.0; gate["lon1"] = -0.0005;
    gate["lat2"] = 51.0; gate["lon2"] =  0.0005;
    model.setSectorGates({gate});

    QSignalSpy spy(&model, &RaceBoxModel::sectorGatesLearned);

    // Establish a heading, then re-learn the finish line at the current spot —
    // the physical line just moved, so the old gates (derived/restored
    // relative to wherever it used to be) must not survive.
    model.onData(makeFix(51.0001, 0.0));
    model.onData(makeFix(51.0002, 0.0));
    QVERIFY(model.canLearnFinishLine());
    model.learnFinishLineHere();

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.first().first().toList().isEmpty());
}

void TestRaceBoxModel::learnFinishLineHereAnnouncesTheClearEvenWithNoGatesLive()
{
    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    // No gates applied — this model has none of its own. That says nothing
    // about what the listener has persisted for this track, so the clear must
    // still go out; a set kept back here would outlive the line it was
    // measured from and be re-applied against the new one on the next startup.
    QSignalSpy spy(&model, &RaceBoxModel::sectorGatesLearned);

    model.onData(makeFix(51.0001, 0.0));
    model.onData(makeFix(51.0002, 0.0));
    QVERIFY(model.canLearnFinishLine());
    model.learnFinishLineHere();

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.first().first().toList().isEmpty());
}

void TestRaceBoxModel::wrongWaySectorGateCrossingIgnored()
{
    QVariantList gates = captureLoopGates();
    QCOMPARE(gates.size(), 2);
    // Derivation latches the direction the gate was built to be crossed in, so
    // it survives persistence rather than being re-guessed from whatever passes
    // over the gate first.
    QVERIFY(gates[0].toMap().value("dir").toInt() != 0);

    // Flip gate 1's accepted direction. The car will now drive over it exactly
    // as before — the same 25 m span a return leg, pit lane or hairpin exit can
    // reach in reality — but from the side the gate rejects.
    QVariantMap flipped = gates[0].toMap();
    flipped["dir"] = -flipped["dir"].toInt();
    gates[0] = flipped;

    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    model.setSectorGates(gates);

    QList<QList<qint64>> received;
    connect(&model, &RaceBoxModel::lapCompleted,
            [&](const RaceBoxLapResult &lap) { received.append(lap.sectorMs); });

    model.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    model.onData(makeFix(kLoopCrossLat, kLoopCrossLon));
    driveFullLoopLap(model);

    // The wrong-way pass must not be consumed as gate 1's crossing. Were it
    // counted, gate 2 would then be crossed normally and the lap would end with
    // a full-length — and entirely wrong — set of splits that passes the
    // all-or-nothing eligibility test and feeds straight into the optimal lap.
    QCOMPARE(received.size(), 1);
    QVERIFY(received[0].isEmpty());
}

void TestRaceBoxModel::setSectorGatesDiscardsTheCurrentLapsSplits()
{
    const QVariantList gates = captureLoopGates();
    QCOMPARE(gates.size(), 2);

    RaceBoxModel model;
    model.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    model.setSectorGates(gates);

    QList<QList<qint64>> received;
    connect(&model, &RaceBoxModel::lapCompleted,
            [&](const RaceBoxLapResult &lap) { received.append(lap.sectorMs); });

    model.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    model.onData(makeFix(kLoopCrossLat, kLoopCrossLon));

    // Drive far enough into the lap to cross the first sector gate...
    driveTo(model, kLoopCrossLat, kLoopCrossLon, kLoopNELat, kLoopNELon, 4, 200);
    driveTo(model, kLoopNELat, kLoopNELon, kLoopNWLat, kLoopNWLon, 4, 200);
    driveTo(model, kLoopNWLat, kLoopNWLon, kLoopSWLat, kLoopSWLon, 4, 200);

    // ...then re-apply gates mid-lap, as happens when the driver picks a
    // different track (or accepts an auto-detect suggestion) while timing.
    model.setSectorGates(gates);

    driveTo(model, kLoopSWLat, kLoopSWLon, kLoopSELat, kLoopSELon, 4, 200);
    driveTo(model, kLoopSELat, kLoopSELon, kLoopCrossLat, kLoopCrossLon, 4, 200);

    // The split banked before the swap belonged to the outgoing gates. Keeping
    // it would let this lap finish with a full-length list stitched from two
    // different gate sets — indistinguishable, downstream, from a clean lap.
    // Discarding it means gate 1 is now unmatched and the lap reports nothing.
    QCOMPARE(received.size(), 1);
    QVERIFY(received[0].isEmpty());
}

QTEST_GUILESS_MAIN(TestRaceBoxModel)
#include "tst_raceboxmodel.moc"

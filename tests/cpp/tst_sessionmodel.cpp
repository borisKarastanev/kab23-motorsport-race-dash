#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "src/sessionmodel.h"
#include "src/candatamodel.h"
#include "src/raceboxmodel.h"
#include "src/trackmodel.h"
#include "src/apppaths.h"

namespace {

// Seeds sessions.json directly (bypassing the lap-completion signal chain
// saveCurrentSession() depends on) so deleteSession() can be exercised
// against known fixture records.
void seedSessions(const QString &json)
{
    QDir().mkpath(AppPaths::dataDir());
    QFile f(AppPaths::dataFile("sessions.json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed sessions.json");
    f.write(json.toUtf8());
}

QJsonArray readPersistedSessions()
{
    QFile f(AppPaths::dataFile("sessions.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

const char *kTwoSpaSessions = R"([
    {"title":"2026-07-20 14:30","timestampIso":"2026-07-20T14:30:00","lapMs":[90000],
     "lapPaths":[],"topSpeedKmh":180,"maxOilC":95.0,"maxCoolantC":88.0,
     "maxLatG":1.1,"maxLonG":0.9,"trackId":"spa","trackName":"Spa-Francorchamps"},
    {"title":"2026-07-21 09:00","timestampIso":"2026-07-21T09:00:00","lapMs":[91000],
     "lapPaths":[],"topSpeedKmh":175,"maxOilC":90.0,"maxCoolantC":85.0,
     "maxLatG":1.0,"maxLonG":0.8,"trackId":"spa","trackName":"Spa-Francorchamps"}
])";

// One Spa session and one Monza session — deleting the sole Spa record must
// leave Spa empty (navigate to the tracks list) while Monza is untouched.
const char *kSpaAndMonzaSessions = R"([
    {"title":"2026-07-20 14:30","timestampIso":"2026-07-20T14:30:00","lapMs":[90000],
     "lapPaths":[],"topSpeedKmh":180,"maxOilC":95.0,"maxCoolantC":88.0,
     "maxLatG":1.1,"maxLonG":0.9,"trackId":"spa","trackName":"Spa-Francorchamps"},
    {"title":"2026-07-21 09:00","timestampIso":"2026-07-21T09:00:00","lapMs":[91000],
     "lapPaths":[],"topSpeedKmh":175,"maxOilC":90.0,"maxCoolantC":85.0,
     "maxLatG":1.0,"maxLonG":0.8,"trackId":"monza","trackName":"Monza"}
])";

// ── Drives RaceBoxModel through real lap completions, via the same rectangular
// loop / finish-line geometry as tst_raceboxmodel.cpp's sector-gate tests (see
// that file for why this shape guarantees the derived gates get re-crossed on
// every lap). Duplicated rather than shared: each test binary here is
// self-contained, same as tst_uplinkmodel.cpp's own copy of this pattern.
constexpr double kGateLat  = 51.0;
constexpr double kGateLonA = -0.001;
constexpr double kGateLonB =  0.001;

RaceBoxData makeFix(double lat, double lon, int speedMmS = 16667 /* ~60 km/h */)
{
    RaceBoxData d{};
    d.fixStatus = 3;
    d.fixFlags  = 0x01;
    d.numSvs    = 8;
    d.latitude  = lat;
    d.longitude = lon;
    d.speedMmS  = speedMmS;
    return d;
}

void driveTo(RaceBoxModel &model, double fromLat, double fromLon, double toLat, double toLon,
            int steps, int stepWaitMs)
{
    for (int i = 1; i <= steps; ++i) {
        const double f = static_cast<double>(i) / steps;
        model.onData(makeFix(fromLat + f * (toLat - fromLat), fromLon + f * (toLon - fromLon)));
        QTest::qWait(stepWaitMs);
    }
}

constexpr double kLoopNELat = 51.0020, kLoopNELon =  0.0000;
constexpr double kLoopNWLat = 51.0020, kLoopNWLon = -0.0030;
constexpr double kLoopSWLat = 50.9980, kLoopSWLon = -0.0030;
constexpr double kLoopSELat = 50.9980, kLoopSELon =  0.0000;
constexpr double kLoopCrossLat = 51.0006, kLoopCrossLon = 0.0000;

QVariantMap gateMap(double lat1, double lon1, double lat2, double lon2)
{
    QVariantMap m;
    m["lat1"] = lat1; m["lon1"] = lon1;
    m["lat2"] = lat2; m["lon2"] = lon2;
    return m;
}

// Three hand-placed gates across the loop's west, north-west and south legs —
// a 4-sector track, standing in for gates restored for a circuit surveyed with
// more than the two RaceBoxModel derives on its own. Each spans the leg it sits
// on perpendicular to the direction of travel there.
QVariantList fourSectorLoopGates()
{
    return {
        gateMap(51.0018, -0.0015, 51.0022, -0.0015), // NE -> NW leg, westbound
        gateMap(51.0005, -0.0032, 51.0005, -0.0028), // NW -> SW leg, southbound
        gateMap(50.9978, -0.0015, 50.9982, -0.0015), // SW -> SE leg, eastbound
    };
}

// stepWaitMs sets the lap's pace: every fix costs real wall-clock time, so a
// shorter wait is a genuinely faster lap. The default 4 steps x 5 legs x 200 ms
// is a ~4 s lap, comfortably clear of RaceBoxModel's 3 s minimum-lap debounce.
void driveFullLoopLap(RaceBoxModel &model, int stepWaitMs = 200)
{
    driveTo(model, kLoopCrossLat, kLoopCrossLon, kLoopNELat, kLoopNELon, 4, stepWaitMs);
    driveTo(model, kLoopNELat, kLoopNELon, kLoopNWLat, kLoopNWLon, 4, stepWaitMs);
    driveTo(model, kLoopNWLat, kLoopNWLon, kLoopSWLat, kLoopSWLon, 4, stepWaitMs);
    driveTo(model, kLoopSWLat, kLoopSWLon, kLoopSELat, kLoopSELon, 4, stepWaitMs);
    driveTo(model, kLoopSELat, kLoopSELon, kLoopCrossLat, kLoopCrossLon, 4, stepWaitMs);
}

}

class TestSessionModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void deleteSessionRemovesMatchingRecord();
    void deleteSessionPersistsRemoval();
    void deleteSessionRebuildsGroupCount();
    void deleteSessionUnknownTimestampIsNoOp();

    // Post-delete navigation predicate (SettingsDetail pops one step back vs.
    // all the way to the tracks list based on this):
    void trackKeepsSessionsAfterDeletingOneOfSeveral();
    void trackHasNoSessionsAfterDeletingLastOne();
    void deletingLastSessionForTrackLeavesOtherTracks();

    void savedSessionCarriesOptimalLapOnceTwoLapsHaveSplits();
    void savedSessionHasNoOptimalLapWithFewerThanTwoEligibleLaps();
    void optimalLapFollowsTheSessionsActualSectorCount();
    void lapNumbersStayUniqueWhenTheFinishLineIsClearedMidSession();
};

void TestSessionModel::initTestCase()
{
    // Sandboxes QStandardPaths::AppDataLocation under a temp dir so this test
    // never touches the real user's sessions.json.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestSessionModel::init()
{
    QFile::remove(AppPaths::dataFile("sessions.json"));
}

void TestSessionModel::deleteSessionRemovesMatchingRecord()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);
    QCOMPARE(sessions.sessions().size(), 2);

    QSignalSpy spy(&sessions, &SessionModel::sessionsChanged);
    sessions.deleteSession("2026-07-20T14:30:00");

    QCOMPARE(spy.count(), 1);
    QCOMPARE(sessions.sessions().size(), 1);
    QCOMPARE(sessions.sessions().first().toMap().value("timestampIso").toString(),
             QStringLiteral("2026-07-21T09:00:00"));
}

void TestSessionModel::deleteSessionPersistsRemoval()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    sessions.deleteSession("2026-07-20T14:30:00");

    const QJsonArray persisted = readPersistedSessions();
    QCOMPARE(persisted.size(), 1);
    QCOMPARE(persisted.first().toObject().value("timestampIso").toString(),
             QStringLiteral("2026-07-21T09:00:00"));
}

void TestSessionModel::deleteSessionRebuildsGroupCount()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);
    QCOMPARE(sessions.sessionGroups().first().toMap().value("count").toInt(), 2);

    sessions.deleteSession("2026-07-20T14:30:00");

    QCOMPARE(sessions.sessionGroups().size(), 1);
    QCOMPARE(sessions.sessionGroups().first().toMap().value("count").toInt(), 1);
}

void TestSessionModel::deleteSessionUnknownTimestampIsNoOp()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    QSignalSpy spy(&sessions, &SessionModel::sessionsChanged);
    sessions.deleteSession("1999-01-01T00:00:00");

    QCOMPARE(spy.count(), 0);
    QCOMPARE(sessions.sessions().size(), 2);
}

void TestSessionModel::trackKeepsSessionsAfterDeletingOneOfSeveral()
{
    seedSessions(kTwoSpaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    sessions.deleteSession("2026-07-20T14:30:00");

    // One Spa session remains → Details pops one step back to the track's list.
    QVERIFY(sessions.hasSessionsForTrack("spa"));
}

void TestSessionModel::trackHasNoSessionsAfterDeletingLastOne()
{
    seedSessions(kSpaAndMonzaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);
    QVERIFY(sessions.hasSessionsForTrack("spa"));

    sessions.deleteSession("2026-07-20T14:30:00"); // the sole Spa session

    // No Spa session left → Details pops all the way to the tracks list.
    QVERIFY(!sessions.hasSessionsForTrack("spa"));
}

void TestSessionModel::deletingLastSessionForTrackLeavesOtherTracks()
{
    seedSessions(kSpaAndMonzaSessions);

    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    sessions.deleteSession("2026-07-20T14:30:00"); // sole Spa session

    // Deleting Spa's last session must not disturb Monza's — its group survives.
    QVERIFY(sessions.hasSessionsForTrack("monza"));
    QCOMPARE(sessions.sessions().size(), 1);
    QCOMPARE(sessions.sessionGroups().size(), 1);
    QCOMPARE(sessions.sessionGroups().first().toMap().value("trackId").toString(),
             QStringLiteral("monza"));
}

void TestSessionModel::savedSessionCarriesOptimalLapOnceTwoLapsHaveSplits()
{
    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    raceBox.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    raceBox.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    raceBox.onData(makeFix(kLoopCrossLat, kLoopCrossLon)); // arms lap 1

    driveFullLoopLap(raceBox); // lap 1 — derives RaceBoxModel's sector gates, reports no splits itself
    driveFullLoopLap(raceBox); // lap 2 — has splits, but still only one eligible lap
    driveFullLoopLap(raceBox); // lap 3 — two eligible laps (2, 3) -> optimal computable at save

    sessions.saveCurrentSession();

    // The optimal lap is a property of the now-completed session, not a live
    // dashboard readout — it's read from the session record, same as any other
    // saved stat (topSpeedKmh, maxOilC, …).
    const QVariantMap saved = sessions.sessions().first().toMap();
    QVERIFY(saved.value("optimalLapMs").toLongLong() > 0);
    const QVariantList sectors = saved.value("optimalSectors").toList();
    QCOMPARE(sectors.size(), 3);
    for (const QVariant &v : sectors) {
        const QVariantMap m = v.toMap();
        QVERIFY(m.contains("sector"));
        QVERIFY(m.contains("lapNumber"));
        QVERIFY(m.contains("sectorMs"));
    }

    // Persisted to disk too, and survives a fresh load — the Sessions view
    // reads sessions.json on startup, not just the in-memory model.
    const QJsonObject persisted = readPersistedSessions().first().toObject();
    QVERIFY(persisted.value("optimalLapMs").toInteger() > 0);
    QCOMPARE(persisted.value("optimalSectors").toArray().size(), 3);
}

void TestSessionModel::savedSessionHasNoOptimalLapWithFewerThanTwoEligibleLaps()
{
    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    raceBox.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    raceBox.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    raceBox.onData(makeFix(kLoopCrossLat, kLoopCrossLon)); // arms lap 1

    // A single lap can only ever derive the sector gates for whoever comes
    // after it — it can never report splits for itself, so with only this one
    // lap recorded there is no optimal lap at all (see optimal-lap-algorithm.md §4).
    driveFullLoopLap(raceBox);

    sessions.saveCurrentSession();

    const QVariantMap saved = sessions.sessions().first().toMap();
    QCOMPARE(saved.value("optimalLapMs").toLongLong(), qint64(0));
    QVERIFY(saved.value("optimalSectors").toList().isEmpty());
}

void TestSessionModel::optimalLapFollowsTheSessionsActualSectorCount()
{
    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    raceBox.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    // Three restored gates -> four sectors. The number of sectors is a runtime
    // property of the gates in force, not a constant: a session whose laps carry
    // four splits must still produce an optimal lap, rather than having every
    // lap silently fail an eligibility test hard-coded to three.
    raceBox.setSectorGates(fourSectorLoopGates());

    raceBox.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    raceBox.onData(makeFix(kLoopCrossLat, kLoopCrossLon)); // arms lap 1

    // Gates are already in force, so no lap is spent deriving them — both laps
    // report splits and both are eligible.
    driveFullLoopLap(raceBox);
    driveFullLoopLap(raceBox);

    sessions.saveCurrentSession();

    const QVariantMap saved = sessions.sessions().first().toMap();
    const QVariantList sectors = saved.value("optimalSectors").toList();
    QCOMPARE(sectors.size(), 4);
    QVERIFY(saved.value("optimalLapMs").toLongLong() > 0);
}

void TestSessionModel::lapNumbersStayUniqueWhenTheFinishLineIsClearedMidSession()
{
    CanDataModel canModel;
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    SessionModel sessions(&canModel, &raceBox, &track);

    raceBox.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
    raceBox.onData(makeFix(50.9998, 0.0));
    QTest::qWait(40);
    raceBox.onData(makeFix(kLoopCrossLat, kLoopCrossLon)); // arms lap 1

    driveFullLoopLap(raceBox); // session lap 1 — derives gates, no splits of its own

    // The driver re-learns the finish line mid-session (the CONFIRM button on
    // DashboardView's clear-finish-line overlay). RaceBoxModel resets its own
    // lap counter to 0 here and drops its sector gates — but the lap already
    // banked above stays in the session, so from here on RaceBoxModel's
    // numbering and the session's diverge.
    raceBox.clearFinishLine();
    raceBox.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);

    driveFullLoopLap(raceBox);      // re-arms on its closing crossing — completes no lap
    driveFullLoopLap(raceBox);      // session lap 2 — derives gates afresh, no splits
    driveFullLoopLap(raceBox);      // session lap 3 — eligible
    driveFullLoopLap(raceBox, 170); // session lap 4 — eligible, and clearly the fastest

    sessions.saveCurrentSession();

    const QVariantMap saved = sessions.sessions().first().toMap();
    const QVariantList lapMs = saved.value("lapMs").toList();
    QCOMPARE(lapMs.size(), 4);

    // Lap 4 is ~600 ms faster than lap 3, so it wins every sector outright.
    // Were the record's number taken from RaceBoxLapResult::lapNumber, the
    // clear above would have restarted the count and filed this lap as 3 —
    // the same number as the genuinely different lap recorded before it.
    const QVariantList sectors = saved.value("optimalSectors").toList();
    QCOMPARE(sectors.size(), 3);
    for (const QVariant &v : sectors)
        QCOMPARE(v.toMap().value("lapNumber").toInt(), 4);

    // Every sector coming from lap 4 makes the optimal lap exactly lap 4.
    QCOMPARE(saved.value("optimalLapMs").toLongLong(), lapMs.at(3).toLongLong());
}

QTEST_GUILESS_MAIN(TestSessionModel)
#include "tst_sessionmodel.moc"

#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QMetaObject>

#include "src/trackmodel.h"
#include "src/raceboxmodel.h"
#include "src/raceboxdata.h"
#include "src/apppaths.h"

namespace {

// TrackModel::loadDatabase() checks this override file before falling back to
// the qrc-bundled DB; this test binary doesn't compile in resources.qrc, so
// seeding the override is the only way (without touching src/) to get
// multi-track data into a TrackModel instance under test.
void seedOverrideTrackDb()
{
    QDir().mkpath(AppPaths::dataDir());
    QFile f(AppPaths::dataFile("track-db.json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed track-db.json");
    f.write(R"([
        {"id":"spa","name":"Spa-Francorchamps","country":"Belgium","c":[50.4372,5.9714]},
        {"id":"silverstone","name":"Silverstone","country":"UK","c":[52.0786,-1.0169]},
        {"id":"monza","name":"Monza","country":"Italy","c":[45.6156,9.2811]}
    ])");
}

// Same as above plus "imola", which carries a confirmed "start" gate — the
// surveyed, locked kind of S/F line (see TrackModel::Track::confirmedFinishLine).
void seedOverrideTrackDbWithConfirmedTrack()
{
    QDir().mkpath(AppPaths::dataDir());
    QFile f(AppPaths::dataFile("track-db.json"));
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not seed track-db.json");
    f.write(R"([
        {"id":"spa","name":"Spa-Francorchamps","country":"Belgium","c":[50.4372,5.9714]},
        {"id":"monza","name":"Monza","country":"Italy","c":[45.6156,9.2811]},
        {"id":"imola","name":"Imola","country":"Italy","c":[44.3439,11.7167],
         "start":[44.3439,11.7167,44.3441,11.7169]}
    ])");
}

QStringList idsOf(const QVariantList &tracks)
{
    QStringList ids;
    for (const QVariant &v : tracks)
        ids << v.toMap().value("id").toString();
    return ids;
}

// Feeds a 3D-fix RaceBoxData sample at (lat, lon) travelling at speedKmh, so
// RaceBoxModel::lastLat/lastLon and the speed TrackModel gates the auto-detect
// scan on are both driven the same way a real fix would.
void feedFix(RaceBoxModel &raceBox, double lat, double lon, double speedKmh)
{
    RaceBoxData d{};
    d.fixStatus = 3;
    d.fixFlags  = 0x01;
    d.numSvs    = 8;
    d.latitude  = lat;
    d.longitude = lon;
    d.speedMmS  = static_cast<qint32>(speedKmh * kMmSPerKmh);
    raceBox.onData(d);
}

QVariantMap gateMap(double lat1, double lon1, double lat2, double lon2)
{
    QVariantMap m;
    m["lat1"] = lat1; m["lon1"] = lon1;
    m["lat2"] = lat2; m["lon2"] = lon2;
    return m;
}

// Directly invokes the private, timer-driven scanNearestTrack() slot rather
// than waiting out the real 5s detect interval — invocation via the
// meta-object system bypasses C++ access control the same way a Qt signal
// connection would.
void triggerScan(TrackModel &track)
{
    QVERIFY(QMetaObject::invokeMethod(&track, "scanNearestTrack"));
}

}

class TestTrackModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void filteringBySearchAndCountry();
    void finishLineGlobalFallback();
    void noSuggestionWhileMoving();
    void noSuggestionWhenTrackAlreadyActive();
    void suggestsWhenStationaryAndNoActiveTrack();
    void suggestionWithdrawnWhenCarStartsMoving();

    void sectorGatesAppliedPairedWithFinishLineOnSelection();
    void sectorGatesPersistAcrossRestart();
    void sectorGatesClearedWhenSwitchingToTrackWithNone();
    void sectorGatesRemovedWhenLearnedEmpty();
    void sectorGatesStoredUnderTheSlotTheFinishLineCameFrom();
    void malformedSectorGateDiscardsTheWholeEntry();

    void confirmedDbFinishLineCannotBeOverwritten();
    void confirmedDbFinishLineOutranksAStoredLearnedOne();
    void unconfirmedTrackAndGlobalSlotStayLearnable();
};

void TestTrackModel::initTestCase()
{
    // Sandboxes QStandardPaths::AppDataLocation under a temp dir so this test
    // never touches the real user's ~/.local/share/bmw-e46-dash.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestTrackModel::init()
{
    seedOverrideTrackDb();
    QFile::remove(AppPaths::dataFile("tracks-user.json"));
}

void TestTrackModel::filteringBySearchAndCountry()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50); // let the deferred (singleShot(0)) DB load run

    track.setSearchText("mon");
    QCOMPARE(idsOf(track.filteredTracks()), QStringList{"monza"});

    track.setSearchText("");
    track.setCountryFilter("UK");
    QCOMPARE(idsOf(track.filteredTracks()), QStringList{"silverstone"});

    track.setCountryFilter("ALL");
    QCOMPARE(idsOf(track.filteredTracks()).size(), 3);
}

void TestTrackModel::finishLineGlobalFallback()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    // No finish line stored yet for any track -> empty map.
    QVERIFY(track.finishLineFor("spa").isEmpty());

    // No track is active (m_activeTrackId is empty by default), so this is
    // learned as the GLOBAL finish line.
    track.onFinishLineLearned(0.1, 0.2, 0.3, 0.4);

    const QVariantMap fl = track.finishLineFor("spa"); // spa has no line of its own
    QVERIFY(!fl.isEmpty());
    QCOMPARE(fl.value("lat1").toDouble(), 0.1);
    QCOMPARE(fl.value("lon1").toDouble(), 0.2);
    QCOMPARE(fl.value("lat2").toDouble(), 0.3);
    QCOMPARE(fl.value("lon2").toDouble(), 0.4);

    // Directly asking for the global key returns the same gate.
    QCOMPARE(track.finishLineFor(QString()), fl);
}

void TestTrackModel::noSuggestionWhileMoving()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    // Right on top of spa, but doing 80 km/h — driving through, not parked.
    feedFix(raceBox, 50.4372, 5.9714, 80.0);
    triggerScan(track);

    QCOMPARE(track.suggestedTrackId(), QString());
}

void TestTrackModel::noSuggestionWhenTrackAlreadyActive()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    // Stationary at spa with no active track -> auto-detect suggests it.
    feedFix(raceBox, 50.4372, 5.9714, 0.0);
    triggerScan(track);
    QCOMPARE(track.suggestedTrackId(), QString("spa"));

    // Accept it (auto-detected, not a manual pick) so a track is now active.
    track.acceptSuggestedTrack();
    QCOMPARE(track.activeTrackId(), QString("spa"));

    // Still stationary, now sitting near a different track (silverstone) —
    // but a track is already selected, so no new suggestion should surface.
    feedFix(raceBox, 52.0786, -1.0169, 0.0);
    triggerScan(track);

    QCOMPARE(track.suggestedTrackId(), QString());
}

void TestTrackModel::suggestsWhenStationaryAndNoActiveTrack()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    feedFix(raceBox, 50.4372, 5.9714, 0.0);
    triggerScan(track);

    QCOMPARE(track.suggestedTrackId(), QString("spa"));
}

void TestTrackModel::suggestionWithdrawnWhenCarStartsMoving()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    // Parked at spa -> the suggestion surfaces (and the scan stops its timer).
    feedFix(raceBox, 50.4372, 5.9714, 0.0);
    triggerScan(track);
    QCOMPARE(track.suggestedTrackId(), QString("spa"));

    // Driver pulls away without tapping CONFIRM/CANCEL. The lingering modal must
    // be withdrawn as the car starts moving, not left on screen while driving —
    // and since the scan timer is stopped while a suggestion is pending, this is
    // driven by the speed change, not by another scan.
    feedFix(raceBox, 50.4372, 5.9714, 40.0);
    QCOMPARE(track.suggestedTrackId(), QString());
}

void TestTrackModel::sectorGatesAppliedPairedWithFinishLineOnSelection()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    track.selectTrack("spa");
    track.onFinishLineLearned(50.437, 5.971, 50.438, 5.972);
    const QVariantList gates{
        gateMap(50.4372, 5.9714, 50.4373, 5.9715),
        gateMap(50.4380, 5.9720, 50.4381, 5.9721),
    };
    track.onSectorGatesLearned(gates);

    // Switch away, then back — re-selecting must re-apply both, paired.
    track.selectTrack("monza");
    QSignalSpy flSpy(&track, &TrackModel::applyFinishLine);
    QSignalSpy sgSpy(&track, &TrackModel::applySectorGates);
    track.selectTrack("spa");

    QCOMPARE(flSpy.count(), 1);
    QCOMPARE(sgSpy.count(), 1);
    QCOMPARE(sgSpy.first().first().toList(), gates);
}

void TestTrackModel::sectorGatesPersistAcrossRestart()
{
    {
        RaceBoxModel raceBox;
        TrackModel track(&raceBox, /*mockMode=*/false);
        QTest::qWait(50);
        track.selectTrack("spa");
        track.onFinishLineLearned(50.437, 5.971, 50.438, 5.972);
        track.onSectorGatesLearned({gateMap(50.4372, 5.9714, 50.4373, 5.9715),
                                    gateMap(50.4380, 5.9720, 50.4381, 5.9721)});
    }

    // A fresh TrackModel over the same (test-mode-sandboxed) tracks-user.json
    // simulates an app restart.
    RaceBoxModel raceBox2;
    TrackModel track2(&raceBox2, /*mockMode=*/false);
    QTest::qWait(50);

    QSignalSpy sgSpy(&track2, &TrackModel::applySectorGates);
    track2.applyStartupFinishLine();

    QCOMPARE(sgSpy.count(), 1);
    QCOMPARE(sgSpy.first().first().toList().size(), 2);
}

void TestTrackModel::sectorGatesClearedWhenSwitchingToTrackWithNone()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    // A global finish line (learned with no track active) plus spa's own line
    // and gates.
    track.onFinishLineLearned(1.0, 1.0, 1.0, 1.1);

    track.selectTrack("spa");
    track.onFinishLineLearned(50.437, 5.971, 50.438, 5.972);
    track.onSectorGatesLearned({gateMap(50.4372, 5.9714, 50.4373, 5.9715)});

    // monza has no finish line of its own -> falls back to the global one, but
    // must not inherit spa's sector gates along with it.
    QSignalSpy sgSpy(&track, &TrackModel::applySectorGates);
    track.selectTrack("monza");

    QCOMPARE(sgSpy.count(), 1);
    QVERIFY(sgSpy.first().first().toList().isEmpty());
}

void TestTrackModel::sectorGatesRemovedWhenLearnedEmpty()
{
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    track.selectTrack("spa");
    track.onFinishLineLearned(50.437, 5.971, 50.438, 5.972);
    track.onSectorGatesLearned({gateMap(50.4372, 5.9714, 50.4373, 5.9715)});

    // The finish line gets relearned — RaceBoxModel emits an empty
    // sectorGatesLearned alongside it (see RaceBoxModel::learnFinishLineHere())
    // — so the old gates must be forgotten, not left stale.
    track.onSectorGatesLearned({});

    track.selectTrack("monza");
    QSignalSpy sgSpy(&track, &TrackModel::applySectorGates);
    track.selectTrack("spa");

    QVERIFY(sgSpy.first().first().toList().isEmpty());
}

void TestTrackModel::sectorGatesStoredUnderTheSlotTheFinishLineCameFrom()
{
    // A track running on the GLOBAL finish line (it has none of its own) derives
    // its gates against that global line. Storing them under the active track id
    // would put them where the global-fallback path never reads them back, so
    // the track would silently re-derive on lap 1 of every future session while
    // tracks-user.json accumulated an unreachable entry.
    {
        RaceBoxModel raceBox;
        TrackModel track(&raceBox, /*mockMode=*/false);
        QTest::qWait(50);

        track.onFinishLineLearned(1.0, 1.0, 1.0, 1.1); // no track active -> global slot
        track.selectTrack("monza");                    // monza has no line -> global fallback
        track.onSectorGatesLearned({gateMap(45.6156, 9.2811, 45.6157, 9.2812),
                                    gateMap(45.6160, 9.2820, 45.6161, 9.2821)});
    }

    // Restart, reselect monza: the gates must come back with the global line
    // they were derived against, not be re-derived from scratch.
    RaceBoxModel raceBox2;
    TrackModel track2(&raceBox2, /*mockMode=*/false);
    QTest::qWait(50);

    QSignalSpy sgSpy(&track2, &TrackModel::applySectorGates);
    track2.selectTrack("monza");

    QCOMPARE(sgSpy.count(), 1);
    QCOMPARE(sgSpy.first().first().toList().size(), 2);
}

void TestTrackModel::malformedSectorGateDiscardsTheWholeEntry()
{
    // A gate list is only meaningful at the length it was derived at. Keeping
    // the intact gates from a partly-corrupt entry would quietly change the
    // track's sector count — every lap then fails the optimal lap's eligibility
    // test, and derivation never re-runs because gates already "exist".
    QDir().mkpath(AppPaths::dataDir());
    QFile f(AppPaths::dataFile("tracks-user.json"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(R"({
        "activeTrackId": "spa",
        "favorites": [],
        "finishLines": { "spa": [50.437, 5.971, 50.438, 5.972] },
        "sectorGates": { "spa": [[50.4372, 5.9714, 50.4373, 5.9715], [50.438, 5.972]] }
    })");
    f.close();

    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    QSignalSpy sgSpy(&track, &TrackModel::applySectorGates);
    track.applyStartupFinishLine();

    QCOMPARE(sgSpy.count(), 1);
    // Not a salvaged 1-gate list — nothing at all, so the next completed lap
    // re-derives a full set.
    QVERIFY(sgSpy.first().first().toList().isEmpty());
}

void TestTrackModel::confirmedDbFinishLineCannotBeOverwritten()
{
    seedOverrideTrackDbWithConfirmedTrack();
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    track.selectTrack("imola");
    QVERIFY(track.activeTrackFinishLineLocked());
    QVERIFY(track.activeTrackHasFinishLine());

    // A learn reaching the model anyway (the UI hides the control, but nothing
    // in C++ depends on that) must be rejected outright — the surveyed line is
    // not the driver's to move.
    track.onFinishLineLearned(44.9999, 11.9999, 44.9998, 11.9998);

    const QVariantMap fl = track.finishLineFor("imola");
    QCOMPARE(fl.value("lat1").toDouble(), 44.3439);
    QCOMPARE(fl.value("lon1").toDouble(), 11.7167);
    QCOMPARE(fl.value("lat2").toDouble(), 44.3441);
    QCOMPARE(fl.value("lon2").toDouble(), 11.7169);

    // A reset (all-zero learn, what clearFinishLine() emits) is refused too.
    track.onFinishLineLearned(0.0, 0.0, 0.0, 0.0);
    QCOMPARE(track.finishLineFor("imola"), fl);
    QVERIFY(track.activeTrackHasFinishLine());
}

void TestTrackModel::confirmedDbFinishLineOutranksAStoredLearnedOne()
{
    // A learned line for imola persisted while the lock was disabled for live
    // testing. With the lock back on, the UI offers no way to reset it, so the
    // confirmed gate has to win rather than stay masked by the stale entry.
    seedOverrideTrackDbWithConfirmedTrack();
    QFile f(AppPaths::dataFile("tracks-user.json"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(R"({
        "activeTrackId": "imola",
        "favorites": [],
        "finishLines": { "imola": [44.5, 11.5, 44.6, 11.6] },
        "sectorGates": {}
    })");
    f.close();

    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    const QVariantMap fl = track.finishLineFor("imola");
    QCOMPARE(fl.value("lat1").toDouble(), 44.3439); // the DB gate, not 44.5
    QCOMPARE(fl.value("lat2").toDouble(), 44.3441);
}

void TestTrackModel::unconfirmedTrackAndGlobalSlotStayLearnable()
{
    // The lock is per-track and keyed on the DB's "start" field. A circuit the
    // DB doesn't pin down, and the global slot used out on the road, must both
    // stay fully learnable.
    seedOverrideTrackDbWithConfirmedTrack();
    RaceBoxModel raceBox;
    TrackModel track(&raceBox, /*mockMode=*/false);
    QTest::qWait(50);

    track.selectTrack("spa"); // in the DB, but with no confirmed "start"
    QVERIFY(!track.activeTrackFinishLineLocked());
    track.onFinishLineLearned(50.437, 5.971, 50.438, 5.972);
    QCOMPARE(track.finishLineFor("spa").value("lat1").toDouble(), 50.437);

    track.clearActiveTrack(); // no track active -> the global slot
    QVERIFY(!track.activeTrackFinishLineLocked());
    track.onFinishLineLearned(1.1, 2.2, 3.3, 4.4);
    QCOMPARE(track.finishLineFor(QString()).value("lat1").toDouble(), 1.1);
}

QTEST_GUILESS_MAIN(TestTrackModel)
#include "tst_trackmodel.moc"

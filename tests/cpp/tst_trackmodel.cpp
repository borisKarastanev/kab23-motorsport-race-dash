#include <QTest>
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

QTEST_GUILESS_MAIN(TestTrackModel)
#include "tst_trackmodel.moc"

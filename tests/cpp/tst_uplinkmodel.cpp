#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "src/candatamodel.h"
#include "src/cloudconfig.h"
#include "src/mockcloudclient.h"
#include "src/raceboxdata.h"
#include "src/raceboxmodel.h"
#include "src/trackmodel.h"
#include "src/uplinkmodel.h"

namespace {

// Same east-west gate the RaceBoxModel tests use, so driving the lap timer into
// Running here goes through the real crossing logic rather than a back door.
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

QJsonObject payloadOf(const MockCloudClient::Message &m)
{
    return QJsonDocument::fromJson(m.payload).object();
}

} // namespace

// UplinkModel owns everything that can silently corrupt a session: framing, the
// sid/seq contract the cloud dedupes on, the monotonic clock the cloud anchors
// time to, and the decision between publishing and spooling. These tests are
// about those, driven through a MockCloudClient so no broker is involved.
class TestUplinkModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void sessionStartsOnLapTimerRunning();
    void sessionStopsOnSessionSaved();
    void seqIsMonotonicWithinASessionAndResetsOnANewOne();
    void monoIsMonotonicAndSurvivesASessionBoundary();
    void offlineFramesGoToTheSpoolNotTheClient();
    void reconnectDrainsTheSpoolBeforeResumingLive();
    void spoolRowsSurviveUntilThePubackArrives();
    void frameCarriesEveryContractedChannel();
    void noCredentialAppearsInAnySignalOrErrorString();
    void aDisabledUplinkTouchesNothing();

private:
    struct Rig;
};

// Everything UplinkModel needs, assembled once per test.
struct TestUplinkModel::Rig {
    QTemporaryDir  dir;
    CanDataModel   can;
    RaceBoxModel   raceBox;
    TrackModel     track{ &raceBox, /*mockMode=*/false };
    CloudConfig    config;
    MockCloudClient client;
    UplinkModel    uplink{ &can, &raceBox, &track, &config, &client };

    Rig()
    {
        config.setBrokerHost("broker.test");
        config.setDeviceId("E46TEST");
        config.setPassword("not-a-real-credential");
        config.setEnabled(true);

        uplink.setSpoolPath(dir.filePath("spool.sqlite"));

        QObject::connect(&raceBox, &RaceBoxModel::lapTimerStateChanged,
                         &uplink, &UplinkModel::onLapTimerStateChanged);
        QObject::connect(&raceBox, &RaceBoxModel::lapCompleted,
                         &uplink, &UplinkModel::onLapCompleted);
    }

    // Drives the real gate-crossing path until the lap timer is Running, which
    // is what opens a cloud session.
    //
    // The wait is load-bearing, not flake-padding. RaceBoxModel emits
    // lapTimerStateChanged from its throttled (<=10 Hz) notify tick, not
    // synchronously from onData() — so the getter reports Running immediately
    // while everything wired to the signal, including the uplink, learns about
    // it up to ~100 ms later. Spinning the event loop here is what makes this a
    // test of the real signal path rather than of a direct call.
    void beginDriving()
    {
        QSignalSpy spy(&raceBox, &RaceBoxModel::lapTimerStateChanged);
        raceBox.setFinishLine(kGateLat, kGateLonA, kGateLat, kGateLonB);
        raceBox.onData(makeFix(50.9998, 0.0)); // south of the gate
        raceBox.onData(makeFix(51.0005, 0.0)); // north-bound crossing -> Running
        QVERIFY(spy.wait(1000));
        QCOMPARE(raceBox.lapTimerState(), RaceBoxModel::Running);
    }

    void goOnline()
    {
        uplink.start();
        client.connectToBroker();
    }

    // start() attempts a connection, and MockCloudClient succeeds instantly —
    // so "offline" has to be arranged before start(), not by simply not calling
    // connectToBroker().
    void goOffline()
    {
        client.setConnectShouldFail(true);
        uplink.start();
    }

    void restoreLink()
    {
        client.setConnectShouldFail(false);
        client.connectToBroker();
    }
};

void TestUplinkModel::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestUplinkModel::init()
{
    // Each test builds its own Rig; nothing shared.
}

void TestUplinkModel::sessionStartsOnLapTimerRunning()
{
    Rig rig;
    rig.goOnline();
    QVERIFY(!rig.uplink.sessionActive());

    rig.beginDriving();

    QVERIFY(rig.uplink.sessionActive());
    const auto events = rig.client.sentOn("/session");
    QCOMPARE(events.size(), 1);

    const QJsonObject start = payloadOf(events.first());
    QCOMPARE(start["event"].toString(), QStringLiteral("start"));
    QCOMPARE(start["v"].toInt(), 1);
    QVERIFY(!start["sid"].toString().isEmpty());
    // QoS 1: the cloud resolves a session from this event, so it must arrive.
    QCOMPARE(events.first().qos, 1);
}

void TestUplinkModel::sessionStopsOnSessionSaved()
{
    Rig rig;
    rig.goOnline();
    rig.beginDriving();

    rig.uplink.onSessionSaved();

    QVERIFY(!rig.uplink.sessionActive());
    const auto events = rig.client.sentOn("/session");
    QCOMPARE(events.size(), 2);
    QCOMPARE(payloadOf(events.last())["event"].toString(), QStringLiteral("stop"));
}

/**
 * `seq` backs the cloud's (session, seq, time) dedup index. If it were not
 * monotonic within a sid, re-sending a backfill batch would stop being free and
 * start overwriting real samples.
 */
void TestUplinkModel::seqIsMonotonicWithinASessionAndResetsOnANewOne()
{
    Rig rig;
    rig.goOnline();
    rig.beginDriving();

    for (int i = 0; i < 5; ++i)
        rig.uplink.sampleOnceForTest();

    auto frames = rig.client.sentOn("/telemetry");
    QCOMPARE(frames.size(), 5);

    const QString firstSid = payloadOf(frames.first())["sid"].toString();
    for (int i = 0; i < frames.size(); ++i) {
        QCOMPARE(payloadOf(frames[i])["seq"].toInt(), i);
        QCOMPARE(payloadOf(frames[i])["sid"].toString(), firstSid);
        // QoS 0: a live frame that misses its moment is worthless, and waiting
        // on a PUBACK per frame at 10 Hz would stall the link.
        QCOMPARE(frames[i].qos, 0);
    }

    // A second session: new sid, seq back to zero.
    rig.uplink.onSessionSaved();
    rig.client.clearSent();
    rig.raceBox.onData(makeFix(50.9990, 0.0));
    rig.raceBox.onData(makeFix(51.0006, 0.0));
    rig.uplink.onLapTimerStateChanged();
    rig.uplink.sampleOnceForTest();

    frames = rig.client.sentOn("/telemetry");
    QVERIFY(!frames.isEmpty());
    QCOMPARE(payloadOf(frames.first())["seq"].toInt(), 0);
}

/**
 * A Pi 4 has no RTC, so the cloud ignores `ts` and computes sample time as
 * session.startedAt + (mono - deviceMonoStartMs). Restarting the monotonic clock
 * at a session boundary would silently stack every sample of the new session on
 * top of the old one's timestamps.
 */
void TestUplinkModel::monoIsMonotonicAndSurvivesASessionBoundary()
{
    Rig rig;
    rig.goOnline();
    rig.beginDriving();

    rig.uplink.sampleOnceForTest();
    QTest::qWait(15);
    rig.uplink.sampleOnceForTest();

    const auto first = rig.client.sentOn("/telemetry");
    const qint64 monoBefore = payloadOf(first.last())["mono"].toInteger();
    QVERIFY(payloadOf(first.first())["mono"].toInteger() <= monoBefore);

    rig.uplink.onSessionSaved();
    QTest::qWait(15);
    rig.raceBox.onData(makeFix(50.9990, 0.0));
    rig.raceBox.onData(makeFix(51.0006, 0.0));
    rig.uplink.onLapTimerStateChanged();
    rig.uplink.sampleOnceForTest();

    const auto after = rig.client.sentOn("/telemetry");
    const qint64 monoAfter = payloadOf(after.last())["mono"].toInteger();

    QVERIFY2(monoAfter > monoBefore,
             "mono must keep counting across a session boundary, never restart");
}

void TestUplinkModel::offlineFramesGoToTheSpoolNotTheClient()
{
    Rig rig;
    rig.goOffline();
    rig.beginDriving();

    for (int i = 0; i < 3; ++i)
        rig.uplink.sampleOnceForTest();

    QVERIFY(rig.client.sent().isEmpty());
    // The session start event plus three telemetry frames.
    QCOMPARE(rig.uplink.queuedFrames(), 4);
}

/**
 * The ordering guarantee: the cloud must see the backlog before the present, or
 * a viewer's map jumps forward and then rewinds.
 */
void TestUplinkModel::reconnectDrainsTheSpoolBeforeResumingLive()
{
    Rig rig;
    rig.goOffline();
    rig.beginDriving();
    for (int i = 0; i < 3; ++i)
        rig.uplink.sampleOnceForTest();
    QCOMPARE(rig.uplink.queuedFrames(), 4);

    rig.restoreLink();

    // First out is the spooled session start, on its own topic and unbatched.
    auto sent = rig.client.sent();
    QCOMPARE(sent.size(), 1);
    QVERIFY(sent.first().topic.endsWith("/session"));
    rig.client.acknowledgeLast();

    // Then the telemetry backlog, as one backfill batch.
    sent = rig.client.sent();
    QCOMPARE(sent.size(), 2);
    QVERIFY(sent.last().topic.endsWith("/backfill"));
    const QJsonObject envelope = payloadOf(sent.last());
    QCOMPARE(envelope["frames"].toArray().size(), 3);
    QCOMPARE(sent.last().qos, 1);
    rig.client.acknowledgeLast();

    QCOMPARE(rig.uplink.queuedFrames(), 0);

    // Only now does live publishing resume.
    rig.uplink.sampleOnceForTest();
    QVERIFY(rig.client.sent().last().topic.endsWith("/telemetry"));
}

/**
 * Rows are deleted on the PUBACK, never on publish, so a power cut between the
 * two re-sends rather than loses. Safe because the cloud dedupes.
 */
void TestUplinkModel::spoolRowsSurviveUntilThePubackArrives()
{
    Rig rig;
    rig.goOffline();
    rig.beginDriving();
    rig.uplink.sampleOnceForTest();

    const int queuedBefore = rig.uplink.queuedFrames();
    QVERIFY(queuedBefore > 0);

    rig.restoreLink();                // publishes the first batch...
    QVERIFY(!rig.client.sent().isEmpty());
    QCOMPARE(rig.uplink.queuedFrames(), queuedBefore); // ...but nothing released

    rig.client.acknowledgeLast();
    QVERIFY(rig.uplink.queuedFrames() < queuedBefore);
}

void TestUplinkModel::frameCarriesEveryContractedChannel()
{
    Rig rig;
    rig.goOnline();
    rig.beginDriving();
    rig.uplink.sampleOnceForTest();

    const QJsonObject frame = payloadOf(rig.client.sentOn("/telemetry").first());

    // The wire contract, reproduced verbatim in both repos' plans. A channel
    // added here and not on the cloud side is silently dropped, so this list is
    // the thing that has to be kept in step.
    for (const char *key : { "v", "sid", "seq", "ts", "mono", "rpm", "coolant",
                             "oil", "speed", "lat", "lon", "gx", "gy", "gz",
                             "lap", "lapMs", "fix", "sats", "ext" }) {
        QVERIFY2(frame.contains(QLatin1String(key)),
                 qPrintable(QStringLiteral("frame is missing '%1'").arg(key)));
    }
}

/**
 * LogBufferModel captures every message into the on-screen Device Log, and
 * lastError is rendered directly in Settings. A credential reaching either is a
 * live secret on a display in a car.
 */
void TestUplinkModel::noCredentialAppearsInAnySignalOrErrorString()
{
    const QString secret = QStringLiteral("s3cr3t-broker-password");

    Rig rig;
    rig.config.setPassword(secret);
    rig.goOnline();
    rig.beginDriving();
    rig.uplink.sampleOnceForTest();
    rig.client.dropLink();
    rig.client.raiseError(QStringLiteral("Broker rejected the credential"));

    QVERIFY(!rig.uplink.lastError().contains(secret));
    QVERIFY(!rig.uplink.brokerSummary().contains(secret));
    QVERIFY(!rig.uplink.stateText().contains(secret));
    QVERIFY(!rig.uplink.deviceId().contains(secret));

    for (const MockCloudClient::Message &m : rig.client.sent())
        QVERIFY2(!QString::fromUtf8(m.payload).contains(secret),
                 "a published payload carried the broker credential");
}

/**
 * Found by running the app: the uplink is opt-in and off by default, but the
 * session-lifecycle slots are wired to signals that fire regardless — so an
 * unpaired dash was spooling a session event for every run it ever did. That is
 * SD-card wear for data that can never be sent, and a backlog that would flood
 * the broker the day someone switched the uplink on.
 */
void TestUplinkModel::aDisabledUplinkTouchesNothing()
{
    Rig rig;
    rig.config.setEnabled(false);
    rig.uplink.start();

    rig.beginDriving();
    rig.uplink.sampleOnceForTest();

    QCOMPARE(rig.uplink.state(), UplinkModel::Disabled);
    QVERIFY(!rig.uplink.sessionActive());
    QVERIFY(rig.client.sent().isEmpty());
    QCOMPARE(rig.uplink.queuedFrames(), 0);
}

QTEST_MAIN(TestUplinkModel)
#include "tst_uplinkmodel.moc"

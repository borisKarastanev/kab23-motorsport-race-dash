#include <QTest>
#include <QSignalSpy>
#include <QCanBusFrame>
#include <QThread>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <functional>

#include "src/can/candatamodel.h"
#include "src/can/canframes.h"
#include "src/core/canscaling.h"

class TestCanDataModel : public QObject {
    Q_OBJECT

private slots:
    void rpmDecodesAndEmitsImmediately();
    void rpmSameValueDoesNotReemit();
    void rpmLargeDropRejectedAsTransient();
    void rpmConsecutiveLargeDropAccepted();
    void rpmClampedTo7000();
    void shortPayloadIgnored();
    void coolantAndOilDecodeAndBatch();
    void setSpeedBatchedByNotifyTimer();
    void oilTempNotSeenUntilFirstDme4Frame();
    void frontWheelsDriveSpeedIgnoringRearSpin();
    void brakingUsesFastestWheel();
    void unchangedWheelAndSteeringFramesDoNotRenotify();
    void gpsSpeedAfterStaleWheelsFlipsSourceTogether();
    void gpsSpeedIgnoredWhileWheelSpeedFresh();
    void gpsFallbackWhenWheelSpeedStale();
    void shortWheelSpeedPayloadIgnored();
    void steeringAngleDecodesSign();
    void brakeBitDecodes();
    void throttleDecodesAndBatches();
    void throttleErrorReadsClosedAndInvalid();
    void inputsInvalidUntilFirstFrame();
    void staleInputsExpireToNeutral();
    void shortDme2FrameStillDecodesCoolant();
    void oilPressureWarningRequiresRunningEngine();
    void oilPressureWarningDebouncedOnStartup();
    void oilPressureWarningClearsWhenBitClears();
    void oilPressureWarningExpiresWhenDme4Stale();
    void oilPressureWarningClearsWhenRpmStale();
    void shortDme4FrameStillDecodesOilTemp();
};

void TestCanDataModel::rpmDecodesAndEmitsImmediately()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::rpmChanged);

    model.onFrame(CanFrames::rpm(3000));

    QCOMPARE(spy.count(), 1); // RPM bypasses the 10 Hz notify timer
    QVERIFY(std::abs(model.rpm() - 3000) <= 1);
}

void TestCanDataModel::rpmSameValueDoesNotReemit()
{
    CanDataModel model;
    model.onFrame(CanFrames::rpm(3000));
    QSignalSpy spy(&model, &CanDataModel::rpmChanged);

    model.onFrame(CanFrames::rpm(3000));

    QCOMPARE(spy.count(), 0);
}

void TestCanDataModel::rpmLargeDropRejectedAsTransient()
{
    CanDataModel model;
    model.onFrame(CanFrames::rpm(3000));
    QSignalSpy spy(&model, &CanDataModel::rpmChanged);

    // m_rpm(3000) > 600 and rpm(500) < m_rpm-2000(1000) -> rejected as a
    // single-frame CAN transient, not a real deceleration.
    model.onFrame(CanFrames::rpm(500));

    QCOMPARE(spy.count(), 0);
    QVERIFY(std::abs(model.rpm() - 3000) <= 1);
}

void TestCanDataModel::rpmConsecutiveLargeDropAccepted()
{
    CanDataModel model;
    model.onFrame(CanFrames::rpm(3000));
    model.onFrame(CanFrames::rpm(500)); // rejected as a one-frame transient
    QVERIFY(std::abs(model.rpm() - 3000) <= 1);

    // A second low reading in a row is a real drop — accepted, so one bogus
    // high value can't pin rpm up forever.
    model.onFrame(CanFrames::rpm(500));
    QVERIFY(std::abs(model.rpm() - 500) <= 1);
}

void TestCanDataModel::rpmClampedTo7000()
{
    CanDataModel model;

    model.onFrame(CanFrames::rpm(8000));

    QCOMPARE(model.rpm(), 7000);
}

void TestCanDataModel::shortPayloadIgnored()
{
    CanDataModel model;
    model.onFrame(CanFrames::rpm(3000));
    const int before = model.rpm();

    // Payload too short to contain the 2-byte RPM field at its offset.
    QCanBusFrame shortFrame(CanScaling::kFrameRpm, QByteArray(2, 0));
    model.onFrame(shortFrame);

    QCOMPARE(model.rpm(), before);
}

void TestCanDataModel::coolantAndOilDecodeAndBatch()
{
    CanDataModel model;
    QSignalSpy coolantSpy(&model, &CanDataModel::coolantTempChanged);
    QSignalSpy oilSpy(&model, &CanDataModel::oilTempChanged);

    model.onFrame(CanFrames::coolant(90.0));
    model.onFrame(CanFrames::dme4(110.0));

    // Coolant/oil are batched by the 10 Hz notify timer, not emitted inline.
    QCOMPARE(coolantSpy.count(), 0);
    QCOMPARE(oilSpy.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(coolantSpy.count(), 1, 500);
    QCOMPARE(oilSpy.count(), 1);
    QVERIFY(std::abs(model.coolantTemp() - 90.0) < 1.0);
    QVERIFY(std::abs(model.oilTemp() - 110.0) < 1e-9);
}

void TestCanDataModel::setSpeedBatchedByNotifyTimer()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::speedChanged);

    model.setSpeed(120);

    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);
    QCOMPARE(model.speed(), 120);
}

void TestCanDataModel::oilTempNotSeenUntilFirstDme4Frame()
{
    CanDataModel model;
    QSignalSpy seenSpy(&model, &CanDataModel::oilTempSeenChanged);

    // oilTemp()'s 0.0 default must not read as a real 0 °C measurement —
    // consumers key the cold-oil limiter reduction off this flag.
    QVERIFY(!model.oilTempSeen());

    // Unrelated traffic must not set it.
    model.onFrame(CanFrames::rpm(3000));
    model.onFrame(CanFrames::coolant(90.0));
    QVERIFY(!model.oilTempSeen());

    // A too-short DME4 payload is ignored, so it must not set it either.
    model.onFrame(QCanBusFrame(CanScaling::kFrameDme4, QByteArray(2, 0)));
    QVERIFY(!model.oilTempSeen());

    model.onFrame(CanFrames::dme4(35.0));
    QTRY_COMPARE_WITH_TIMEOUT(seenSpy.count(), 1, 500);
    QVERIFY(model.oilTempSeen());

    // Latches once true, and doesn't re-notify on subsequent frames. The temp
    // itself is assigned synchronously in onFrame (only the notification is
    // batched), so this needs no QTRY_ retry loop.
    model.onFrame(CanFrames::dme4(80.0));
    QVERIFY(std::abs(model.oilTemp() - 80.0) < 1e-9);
    QTest::qWait(200); // let a notify tick elapse — still no second signal
    QCOMPARE(seenSpy.count(), 1);
}

void TestCanDataModel::frontWheelsDriveSpeedIgnoringRearSpin()
{
    CanDataModel model;
    QSignalSpy speedSpy(&model, &CanDataModel::speedChanged);
    QSignalSpy sourceSpy(&model, &CanDataModel::speedFromWheelsChanged);

    // Rears spinning up on launch must not inflate the displayed speed.
    model.onFrame(CanFrames::wheelSpeeds(20.0, 22.0, 60.0, 60.0));

    QCOMPARE(model.speed(), 21);
    QVERIFY(model.speedFromWheels());
    QCOMPARE(model.wheelSpeedRL(), 60.0);
    QTRY_COMPARE_WITH_TIMEOUT(speedSpy.count(), 1, 500);
    QCOMPARE(sourceSpy.count(), 1);
}

void TestCanDataModel::brakingUsesFastestWheel()
{
    CanDataModel model;
    model.onFrame(CanFrames::asc1(CanScaling::kBrakeBit));

    // Fronts locking under braking — the fastest wheel is closest to road speed.
    model.onFrame(CanFrames::wheelSpeeds(0.0, 10.0, 95.0, 90.0));

    QCOMPARE(model.speed(), 95);
}

void TestCanDataModel::unchangedWheelAndSteeringFramesDoNotRenotify()
{
    CanDataModel model;
    QSignalSpy wheelSpy(&model, &CanDataModel::wheelSpeedsChanged);
    QSignalSpy steerSpy(&model, &CanDataModel::steeringAngleChanged);

    model.onFrame(CanFrames::wheelSpeeds(50.0, 50.0, 50.0, 50.0));
    model.onFrame(CanFrames::steering(10.0));
    QTRY_COMPARE_WITH_TIMEOUT(wheelSpy.count(), 1, 500);
    QCOMPARE(steerSpy.count(), 1);

    // Parked car streaming identical frames — no further notifications.
    for (int i = 0; i < 5; ++i) {
        model.onFrame(CanFrames::wheelSpeeds(50.0, 50.0, 50.0, 50.0));
        model.onFrame(CanFrames::steering(10.0));
        QTest::qWait(30);
    }
    QTest::qWait(150);
    QCOMPARE(wheelSpy.count(), 1);
    QCOMPARE(steerSpy.count(), 1);
}

void TestCanDataModel::gpsSpeedAfterStaleWheelsFlipsSourceTogether()
{
    CanDataModel model;
    model.onFrame(CanFrames::wheelSpeeds(80.0, 80.0, 80.0, 80.0));

    // Block without spinning the event loop, so the notify timer can't do the
    // handover first: setSpeed itself must flip the source when it lands after
    // the wheels went stale.
    QThread::msleep(CanDataModel::kWheelSpeedStaleMs + 50);
    model.setSpeed(40);

    QCOMPARE(model.speed(), 40);
    QVERIFY(!model.speedFromWheels());
}

void TestCanDataModel::gpsSpeedIgnoredWhileWheelSpeedFresh()
{
    CanDataModel model;
    model.onFrame(CanFrames::wheelSpeeds(80.0, 80.0, 80.0, 80.0));

    model.setSpeed(95);

    QCOMPARE(model.speed(), 80);
}

void TestCanDataModel::gpsFallbackWhenWheelSpeedStale()
{
    CanDataModel model;
    model.onFrame(CanFrames::wheelSpeeds(80.0, 80.0, 80.0, 80.0));
    model.setSpeed(95); // remembered, not displayed yet
    QCOMPARE(model.speed(), 80);

    // No further 0x1F0 frames — the notify tick past the stale threshold hands
    // the gauge back to the GPS reading.
    QTRY_VERIFY_WITH_TIMEOUT(!model.speedFromWheels(),
                             CanDataModel::kWheelSpeedStaleMs + 500);
    QCOMPARE(model.speed(), 95);

    model.setSpeed(60);
    QCOMPARE(model.speed(), 60);
}

void TestCanDataModel::shortWheelSpeedPayloadIgnored()
{
    CanDataModel model;
    model.onFrame(QCanBusFrame(CanScaling::kFrameWheelSpeed, QByteArray(6, 0x10)));

    QVERIFY(!model.speedFromWheels());
    QCOMPARE(model.speed(), 0);
}

void TestCanDataModel::steeringAngleDecodesSign()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::steeringAngleChanged);

    model.onFrame(CanFrames::steering(-45.0));
    QVERIFY(std::abs(model.steeringAngle() + 45.0) < CanScaling::kDegPerSteeringLsb);

    model.onFrame(CanFrames::steering(30.0));
    QVERIFY(std::abs(model.steeringAngle() - 30.0) < CanScaling::kDegPerSteeringLsb);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500); // batched
}

void TestCanDataModel::brakeBitDecodes()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::brakePressedChanged);

    model.onFrame(CanFrames::asc1(0xEF)); // every bit except the brake bit
    QVERIFY(!model.brakePressed());

    model.onFrame(CanFrames::asc1(CanScaling::kBrakeBit));
    QVERIFY(model.brakePressed());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);

    model.onFrame(CanFrames::asc1(0x00));
    QVERIFY(!model.brakePressed());
}

void TestCanDataModel::throttleDecodesAndBatches()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::throttleChanged);
    QVERIFY(!model.throttleValid());

    model.onFrame(CanFrames::dme2(90.0, 25.0));
    QCOMPARE(spy.count(), 0); // batched by the 10 Hz notify timer
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);
    QVERIFY(model.throttleValid());
    QVERIFY(std::abs(model.throttle() - 25.0) < 0.5);

    // An unchanged reading must not re-notify.
    model.onFrame(CanFrames::dme2(90.0, 25.0));
    QTest::qWait(150);
    QCOMPARE(spy.count(), 1);
}

void TestCanDataModel::throttleErrorReadsClosedAndInvalid()
{
    CanDataModel model;
    model.onFrame(CanFrames::dme2(90.0, 60.0));
    QVERIFY(model.throttleValid());

    // A PVS fault must not leave the pre-fault pedal position on display.
    QByteArray payload = CanFrames::dme2(90.0, 0.0).payload();
    payload[CanScaling::kOffsetThrottle] = static_cast<char>(CanScaling::kThrottleRawError);
    model.onFrame(QCanBusFrame(CanScaling::kFrameTemp, payload));

    QVERIFY(!model.throttleValid());
    QCOMPARE(model.throttle(), 0.0);
}

void TestCanDataModel::inputsInvalidUntilFirstFrame()
{
    CanDataModel model;
    QVERIFY(!model.throttleValid());
    QVERIFY(!model.steeringValid());
    QVERIFY(!model.brakeValid());

    // An unchanged-from-default value still marks the input as live.
    model.onFrame(CanFrames::steering(0.0));
    model.onFrame(CanFrames::asc1(0x00));
    QVERIFY(model.steeringValid());
    QVERIFY(model.brakeValid());
}

void TestCanDataModel::staleInputsExpireToNeutral()
{
    CanDataModel model;
    model.onFrame(CanFrames::dme2(90.0, 80.0));
    model.onFrame(CanFrames::steering(45.0));
    model.onFrame(CanFrames::asc1(CanScaling::kBrakeBit));
    QVERIFY(model.throttleValid() && model.steeringValid() && model.brakeValid());

    // Bus goes quiet — the notify tick past the threshold drops all three.
    QTRY_VERIFY_WITH_TIMEOUT(!model.throttleValid(), CanDataModel::kInputStaleMs + 500);
    QVERIFY(!model.steeringValid());
    QVERIFY(!model.brakeValid());
    QCOMPARE(model.throttle(), 0.0);
    QCOMPARE(model.steeringAngle(), 0.0);
    QVERIFY(!model.brakePressed());
}

void TestCanDataModel::shortDme2FrameStillDecodesCoolant()
{
    CanDataModel model;
    model.onFrame(QCanBusFrame(CanScaling::kFrameTemp, CanFrames::coolant(80.0).payload().left(2)));

    QVERIFY(std::abs(model.coolantTemp() - 80.0) < 1.0);
    QVERIFY(!model.throttleValid());
    QCOMPARE(model.throttle(), 0.0);
}

// Streams `frames` every 100 ms like a live bus (well inside the 500 ms stale
// window) until `done()` holds or `ms` runs out; returns done(). The warning
// needs fresh rpm for the whole debounce window, so frames can't be sent once.
static bool feedUntil(CanDataModel &model, const QList<QCanBusFrame> &frames, int ms,
                      const std::function<bool()> &done)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        for (const QCanBusFrame &f : frames)
            model.onFrame(f);
        QTest::qWait(100);
    }
    return done();
}

static constexpr int kSettleMs = CanDataModel::kOilPressureDebounceMs + 300;

void TestCanDataModel::oilPressureWarningRequiresRunningEngine()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::oilPressureWarningChanged);
    const auto warning = [&] { return model.oilPressureWarning(); };

    // Ignition on, engine stopped: the switch reads low, but that's expected.
    QVERIFY(!feedUntil(model, {CanFrames::rpm(0), CanFrames::dme4(90.0, true)}, kSettleMs, warning));
    QCOMPARE(spy.count(), 0);

    QVERIFY(feedUntil(model, {CanFrames::rpm(800), CanFrames::dme4(90.0, true)}, kSettleMs, warning));
    QCOMPARE(spy.count(), 1);
}

void TestCanDataModel::oilPressureWarningDebouncedOnStartup()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::oilPressureWarningChanged);
    const auto warning = [&] { return model.oilPressureWarning(); };

    // Engine just caught: pressure builds within the debounce window.
    QVERIFY(!feedUntil(model, {CanFrames::rpm(900), CanFrames::dme4(90.0, true)},
                       CanDataModel::kOilPressureDebounceMs / 2, warning));
    QVERIFY(!feedUntil(model, {CanFrames::rpm(900), CanFrames::dme4(90.0, false)}, kSettleMs, warning));
    QCOMPARE(spy.count(), 0);
}

void TestCanDataModel::oilPressureWarningClearsWhenBitClears()
{
    CanDataModel model;
    const auto warning = [&] { return model.oilPressureWarning(); };
    const auto cleared = [&] { return !model.oilPressureWarning(); };
    QVERIFY(feedUntil(model, {CanFrames::rpm(2000), CanFrames::dme4(90.0, true)}, kSettleMs, warning));

    // Held through a brief all-clear (switch chatter)...
    const QList<QCanBusFrame> ok{CanFrames::rpm(2000), CanFrames::dme4(90.0, false)};
    QVERIFY(!feedUntil(model, ok, CanDataModel::kOilPressureDebounceMs / 2, cleared));

    // ...and cleared once pressure is back for the full window.
    QVERIFY(feedUntil(model, ok, kSettleMs, cleared));
}

void TestCanDataModel::oilPressureWarningExpiresWhenDme4Stale()
{
    CanDataModel model;
    QVERIFY(feedUntil(model, {CanFrames::rpm(2000), CanFrames::dme4(90.0, true)}, kSettleMs,
                      [&] { return model.oilPressureWarning(); }));

    // 0x545 goes quiet — a dead bus must not latch the alarm on.
    QVERIFY(feedUntil(model, {CanFrames::rpm(2000)}, CanDataModel::kInputStaleMs + kSettleMs,
                      [&] { return !model.oilPressureWarning(); }));
}

void TestCanDataModel::oilPressureWarningClearsWhenRpmStale()
{
    CanDataModel model;
    QVERIFY(feedUntil(model, {CanFrames::rpm(2000), CanFrames::dme4(90.0, true)}, kSettleMs,
                      [&] { return model.oilPressureWarning(); }));

    // 0x316 goes quiet — a stale rpm can't vouch for a running engine.
    QVERIFY(feedUntil(model, {CanFrames::dme4(90.0, true)}, CanDataModel::kInputStaleMs + kSettleMs,
                      [&] { return !model.oilPressureWarning(); }));
}

void TestCanDataModel::shortDme4FrameStillDecodesOilTemp()
{
    CanDataModel model;
    const QCanBusFrame shortFrame(CanScaling::kFrameDme4,
                                  CanFrames::dme4(70.0, true).payload().left(5));

    // Logged once, not per frame.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("oil pressure warning unavailable"));
    model.onFrame(shortFrame);
    QVERIFY(std::abs(model.oilTemp() - 70.0) < 1e-9);

    QVERIFY(!feedUntil(model, {CanFrames::rpm(2000), shortFrame}, kSettleMs,
                       [&] { return model.oilPressureWarning(); }));
}

QTEST_GUILESS_MAIN(TestCanDataModel)
#include "tst_candatamodel.moc"

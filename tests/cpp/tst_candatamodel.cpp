#include <QTest>
#include <QSignalSpy>
#include <QCanBusFrame>

#include "src/can/candatamodel.h"
#include "src/core/canscaling.h"

namespace {

QCanBusFrame rpmFrame(int rpm)
{
    QByteArray payload(8, 0);
    char buf[2];
    CanScaling::encodeRpm(rpm, buf);
    payload[CanScaling::kOffsetRpm]     = buf[0];
    payload[CanScaling::kOffsetRpm + 1] = buf[1];
    return QCanBusFrame(CanScaling::kFrameRpm, payload);
}

QCanBusFrame coolantFrame(double celsius)
{
    QByteArray payload(8, 0);
    payload[CanScaling::kOffsetCoolant] = static_cast<char>(CanScaling::encodeCoolant(celsius));
    return QCanBusFrame(CanScaling::kFrameTemp, payload);
}

QCanBusFrame oilTempFrame(double celsius)
{
    QByteArray payload(8, 0);
    payload[CanScaling::kOffsetOilTemp] = static_cast<char>(CanScaling::encodeOilTemp(celsius));
    return QCanBusFrame(CanScaling::kFrameDme4, payload);
}

}

class TestCanDataModel : public QObject {
    Q_OBJECT

private slots:
    void rpmDecodesAndEmitsImmediately();
    void rpmSameValueDoesNotReemit();
    void rpmLargeDropRejectedAsTransient();
    void rpmClampedTo7000();
    void shortPayloadIgnored();
    void coolantAndOilDecodeAndBatch();
    void setSpeedBatchedByNotifyTimer();
    void oilTempNotSeenUntilFirstDme4Frame();
};

void TestCanDataModel::rpmDecodesAndEmitsImmediately()
{
    CanDataModel model;
    QSignalSpy spy(&model, &CanDataModel::rpmChanged);

    model.onFrame(rpmFrame(3000));

    QCOMPARE(spy.count(), 1); // RPM bypasses the 10 Hz notify timer
    QVERIFY(std::abs(model.rpm() - 3000) <= 1);
}

void TestCanDataModel::rpmSameValueDoesNotReemit()
{
    CanDataModel model;
    model.onFrame(rpmFrame(3000));
    QSignalSpy spy(&model, &CanDataModel::rpmChanged);

    model.onFrame(rpmFrame(3000));

    QCOMPARE(spy.count(), 0);
}

void TestCanDataModel::rpmLargeDropRejectedAsTransient()
{
    CanDataModel model;
    model.onFrame(rpmFrame(3000));
    QSignalSpy spy(&model, &CanDataModel::rpmChanged);

    // m_rpm(3000) > 600 and rpm(500) < m_rpm-2000(1000) -> rejected as a
    // single-frame CAN transient, not a real deceleration.
    model.onFrame(rpmFrame(500));

    QCOMPARE(spy.count(), 0);
    QVERIFY(std::abs(model.rpm() - 3000) <= 1);
}

void TestCanDataModel::rpmClampedTo7000()
{
    CanDataModel model;

    model.onFrame(rpmFrame(8000));

    QCOMPARE(model.rpm(), 7000);
}

void TestCanDataModel::shortPayloadIgnored()
{
    CanDataModel model;
    model.onFrame(rpmFrame(3000));
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

    model.onFrame(coolantFrame(90.0));
    model.onFrame(oilTempFrame(110.0));

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
    model.onFrame(rpmFrame(3000));
    model.onFrame(coolantFrame(90.0));
    QVERIFY(!model.oilTempSeen());

    // A too-short DME4 payload is ignored, so it must not set it either.
    model.onFrame(QCanBusFrame(CanScaling::kFrameDme4, QByteArray(2, 0)));
    QVERIFY(!model.oilTempSeen());

    model.onFrame(oilTempFrame(35.0));
    QTRY_COMPARE_WITH_TIMEOUT(seenSpy.count(), 1, 500);
    QVERIFY(model.oilTempSeen());

    // Latches once true, and doesn't re-notify on subsequent frames. The temp
    // itself is assigned synchronously in onFrame (only the notification is
    // batched), so this needs no QTRY_ retry loop.
    model.onFrame(oilTempFrame(80.0));
    QVERIFY(std::abs(model.oilTemp() - 80.0) < 1e-9);
    QTest::qWait(200); // let a notify tick elapse — still no second signal
    QCOMPARE(seenSpy.count(), 1);
}

QTEST_GUILESS_MAIN(TestCanDataModel)
#include "tst_candatamodel.moc"

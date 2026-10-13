#include <QTest>
#include <cmath>

#include "src/core/canscaling.h"

class TestCanScaling : public QObject {
    Q_OBJECT

private slots:
    void rpmRoundTrip_data();
    void rpmRoundTrip();
    void coolantRoundTrip_data();
    void coolantRoundTrip();
    void oilTempRoundTrip_data();
    void oilTempRoundTrip();
    void decodeBoundaryBytes();
    void frameAndOffsetConstants();
    void wheelSpeedRoundTrip();
    void wheelSpeedMasksUpperNibble();
    void wheelSpeedEncodeClampsInsteadOfWrapping();
    void steeringRoundTrip_data();
    void steeringRoundTrip();
};

void TestCanScaling::rpmRoundTrip_data()
{
    QTest::addColumn<int>("rpm");
    QTest::newRow("zero")    << 0;
    QTest::newRow("idle")    << 800;
    QTest::newRow("cruise")  << 3000;
    QTest::newRow("redline") << 6800;
    QTest::newRow("clamped-ceiling-ish") << 7000;
}

void TestCanScaling::rpmRoundTrip()
{
    QFETCH(int, rpm);
    char buf[2] = {0, 0};
    CanScaling::encodeRpm(rpm, buf);
    const int decoded = CanScaling::decodeRpm(buf);
    // kRawPerRpm (6.4) isn't exactly representable in binary floating point,
    // so encode->decode can be off by a fraction of an RPM; allow up to 1.
    QVERIFY2(std::abs(decoded - rpm) <= 1,
             qPrintable(QString("rpm=%1 decoded=%2").arg(rpm).arg(decoded)));
}

void TestCanScaling::coolantRoundTrip_data()
{
    QTest::addColumn<double>("celsius");
    QTest::newRow("cold")     << 20.0;
    QTest::newRow("warm")     << 60.0;
    QTest::newRow("hot")      << 90.0;
}

void TestCanScaling::coolantRoundTrip()
{
    QFETCH(double, celsius);
    const quint8 byte = CanScaling::encodeCoolant(celsius);
    const double decoded = CanScaling::decodeCoolant(byte);
    // Quantized to a single byte (0.75 °C/count) — round trip within one count.
    QVERIFY2(std::abs(decoded - celsius) < CanScaling::kCoolantScale + 1e-9,
             qPrintable(QString("celsius=%1 decoded=%2").arg(celsius).arg(decoded)));
}

void TestCanScaling::oilTempRoundTrip_data()
{
    QTest::addColumn<double>("celsius");
    QTest::newRow("cold") << 20.0;
    QTest::newRow("hot")  << 110.0;
}

void TestCanScaling::oilTempRoundTrip()
{
    QFETCH(double, celsius);
    const quint8 byte = CanScaling::encodeOilTemp(celsius);
    const double decoded = CanScaling::decodeOilTemp(byte);
    // Integer offset only — exact.
    QCOMPARE(decoded, celsius);
}

void TestCanScaling::decodeBoundaryBytes()
{
    QVERIFY(std::abs(CanScaling::decodeCoolant(0)   - (-48.373)) < 1e-9);
    QVERIFY(std::abs(CanScaling::decodeCoolant(255) - (0.75 * 255 - 48.373)) < 1e-9);
    QCOMPARE(CanScaling::decodeOilTemp(0),   -48.0);
    QCOMPARE(CanScaling::decodeOilTemp(255),  207.0);
}

void TestCanScaling::frameAndOffsetConstants()
{
    QCOMPARE(CanScaling::kFrameRpm,  0x316u);
    QCOMPARE(CanScaling::kFrameTemp, 0x329u);
    QCOMPARE(CanScaling::kFrameDme4, 0x545u);
    QCOMPARE(CanScaling::kOffsetRpm,     2);
    QCOMPARE(CanScaling::kOffsetCoolant, 1);
    QCOMPARE(CanScaling::kOffsetOilTemp, 4);
}

void TestCanScaling::wheelSpeedRoundTrip()
{
    char buf[2] = {0, 0};
    CanScaling::encodeWheelSpeed(123.4375, buf); // exact multiple of 0.0625
    QCOMPARE(CanScaling::decodeWheelSpeed(buf), 123.4375);
}

void TestCanScaling::wheelSpeedMasksUpperNibble()
{
    // 0xF640: upper nibble 0xF is counter/flags, low 12 bits 0x640 = 1600 → 100 km/h.
    const char buf[2] = {static_cast<char>(0x40), static_cast<char>(0xF6)};
    QCOMPARE(CanScaling::decodeWheelSpeed(buf), 100.0);
}

void TestCanScaling::wheelSpeedEncodeClampsInsteadOfWrapping()
{
    char buf[2] = {0, 0};
    CanScaling::encodeWheelSpeed(260.0, buf); // above the 12-bit ceiling (~255.94)
    QCOMPARE(CanScaling::decodeWheelSpeed(buf), CanScaling::kWheelSpeedMask * CanScaling::kKmhPerWheelLsb);

    CanScaling::encodeWheelSpeed(-5.0, buf);
    QCOMPARE(CanScaling::decodeWheelSpeed(buf), 0.0);
}

void TestCanScaling::steeringRoundTrip_data()
{
    QTest::addColumn<double>("deg");
    QTest::newRow("centre") << 0.0;
    QTest::newRow("left")   << 90.0;
    QTest::newRow("right")  << -90.0;
    QTest::newRow("lock")   << -540.0;
}

void TestCanScaling::steeringRoundTrip()
{
    QFETCH(double, deg);
    char buf[2] = {0, 0};
    CanScaling::encodeSteering(deg, buf);
    QVERIFY(std::abs(CanScaling::decodeSteering(buf) - deg) <= CanScaling::kDegPerSteeringLsb / 2 + 1e-9);
}

QTEST_APPLESS_MAIN(TestCanScaling)
#include "tst_canscaling.moc"

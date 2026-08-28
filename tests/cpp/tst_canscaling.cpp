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

QTEST_APPLESS_MAIN(TestCanScaling)
#include "tst_canscaling.moc"

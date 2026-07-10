#include <QTest>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <cmath>

#include "src/displaymodel.h"
#include "src/apppaths.h"

namespace {

// Builds a fake sysfs backlight tree at <root>/<device>/{brightness,max_brightness}
// so DisplayModel's discovery/read/write logic can be tested without real
// hardware. Returns the device directory path.
QString makeFakeBacklightDevice(const QString &root, const QString &device, int maxBrightness)
{
    const QString devDir = root + '/' + device;
    QDir().mkpath(devDir);
    QFile maxFile(devDir + "/max_brightness");
    maxFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    maxFile.write(QByteArray::number(maxBrightness));
    QFile brightnessFile(devDir + "/brightness");
    brightnessFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    brightnessFile.write("0");
    return devDir;
}

int readBrightnessFile(const QString &devDir)
{
    QFile f(devDir + "/brightness");
    f.open(QIODevice::ReadOnly);
    return f.readAll().trimmed().toInt();
}

}

class TestDisplayModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void clampsOutOfRangeValues();
    void scalesToHardwareWithSafetyFloor();
    void startupAppliesPersistedValueToHardware();
    void persistenceRoundTrips();
    void debouncedPersistFiresViaTimer();
    void notifyOnlyOnActualChange();
    void gracefulNoBacklight();
};

void TestDisplayModel::initTestCase()
{
    // Sandboxes QStandardPaths::AppDataLocation under a temp dir so this test
    // never touches the real user's ~/.local/share/bmw-e46-dash.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestDisplayModel::init()
{
    QFile::remove(AppPaths::dataFile("display.conf"));
}

void TestDisplayModel::clampsOutOfRangeValues()
{
    QTemporaryDir root;
    makeFakeBacklightDevice(root.path(), "rpi_backlight", 255);
    DisplayModel model(root.path());

    model.setBrightness(-10);
    QCOMPARE(model.brightness(), 0);

    model.setBrightness(150);
    QCOMPARE(model.brightness(), 100);
}

void TestDisplayModel::scalesToHardwareWithSafetyFloor()
{
    QTemporaryDir root;
    const QString devDir = makeFakeBacklightDevice(root.path(), "rpi_backlight", 255);
    DisplayModel model(root.path());
    QVERIFY(model.hasBacklight());

    // floor = round(255 * 0.10) = 26 (a headless kiosk dash has no keyboard,
    // so 0% must never fully black out the only display)
    const int floor = static_cast<int>(std::lround(255 * 0.10));

    model.setBrightness(0);
    QCOMPARE(readBrightnessFile(devDir), floor);

    model.setBrightness(100);
    QCOMPARE(readBrightnessFile(devDir), 255);

    model.setBrightness(50);
    const int expectedHalf = floor + static_cast<int>(std::lround(0.5 * (255 - floor)));
    QCOMPARE(readBrightnessFile(devDir), expectedHalf);
}

void TestDisplayModel::startupAppliesPersistedValueToHardware()
{
    QTemporaryDir root;
    const QString devDir = makeFakeBacklightDevice(root.path(), "10-0045", 255);

    {
        DisplayModel model(root.path());
        model.setBrightness(42);
    }

    // A second instance over the same sandbox must read the persisted value
    // AND push it to the hardware on construction, so the panel matches the
    // saved setting after every reboot.
    DisplayModel reloaded(root.path());
    QCOMPARE(reloaded.brightness(), 42);

    const int floor = static_cast<int>(std::lround(255 * 0.10));
    const int expected = floor + static_cast<int>(std::lround(0.42 * (255 - floor)));
    QCOMPARE(readBrightnessFile(devDir), expected);
}

void TestDisplayModel::persistenceRoundTrips()
{
    QTemporaryDir root;
    makeFakeBacklightDevice(root.path(), "rpi_backlight", 255);

    {
        DisplayModel model(root.path());
        model.setBrightness(17);
    }
    DisplayModel reloaded(root.path());
    QCOMPARE(reloaded.brightness(), 17);
}

void TestDisplayModel::debouncedPersistFiresViaTimer()
{
    QTemporaryDir root;
    makeFakeBacklightDevice(root.path(), "rpi_backlight", 255);
    DisplayModel model(root.path());

    model.setBrightness(33);

    // With an event loop running, the debounce timer (500 ms) fires and writes
    // display.conf on its own — no destructor flush and no per-move write. The
    // model is deliberately still alive here so a pass proves the TIMER path.
    QTest::qWait(700);

    QSettings s(AppPaths::dataFile("display.conf"), QSettings::IniFormat);
    QCOMPARE(s.value("Display/Brightness").toInt(), 33);
}

void TestDisplayModel::notifyOnlyOnActualChange()
{
    QTemporaryDir root;
    makeFakeBacklightDevice(root.path(), "rpi_backlight", 255);
    DisplayModel model(root.path());
    model.setBrightness(60);

    QSignalSpy spy(&model, &DisplayModel::brightnessChanged);

    model.setBrightness(60); // unchanged
    QCOMPARE(spy.count(), 0);

    model.setBrightness(61); // changed
    QCOMPARE(spy.count(), 1);
}

void TestDisplayModel::gracefulNoBacklight()
{
    QTemporaryDir emptyRoot; // no fake device created under here

    {
        DisplayModel model(emptyRoot.path());
        QCOMPARE(model.hasBacklight(), false);
        model.setBrightness(70); // must not crash despite no hardware to write to
        QCOMPARE(model.brightness(), 70);
    } // scope-exit flushes the debounced persist (no event loop in this test)

    DisplayModel reloaded(emptyRoot.path());
    QCOMPARE(reloaded.brightness(), 70); // still persisted even with no hardware
}

QTEST_GUILESS_MAIN(TestDisplayModel)
#include "tst_displaymodel.moc"

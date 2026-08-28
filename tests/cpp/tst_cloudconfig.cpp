#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "src/core/apppaths.h"
#include "src/cloud/cloudconfig.h"

// cloud.conf holds a live broker credential, so these tests are about the
// handling rules rather than the getters: the file must be 0600, the secret
// must survive a restart, and it must not be reachable through the QML
// meta-object surface.
class TestCloudConfig : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void persistsAndReloadsPairing();
    void configFileIsOwnerReadWriteOnly();
    void persistLeavesNoStagingCopyBesideTheConfig();
    void passwordIsNotReadableThroughTheMetaObject();
    void configuredRequiresHostDeviceAndPassword();
    void clearPasswordUnpairsWithoutLosingTheBroker();
    void tlsIsOnByDefault();

private:
    static QString confPath() { return AppPaths::dataFile("cloud.conf"); }
};

void TestCloudConfig::initTestCase()
{
    // Sandboxes AppDataLocation so this never touches the developer's real
    // paired credential.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName("bmw-e46-dash");
}

void TestCloudConfig::init()
{
    QFile::remove(confPath());
    // Left behind by builds predating the QTemporaryDir staging fix. Removed
    // here so persistLeavesNoStagingCopyBesideTheConfig() tests what this run of
    // persist() writes rather than what an older one did.
    QFile::remove(confPath() + QStringLiteral(".build"));
}

void TestCloudConfig::persistsAndReloadsPairing()
{
    {
        CloudConfig config;
        config.setBrokerHost("telemetry.example.com");
        config.setBrokerPort(8883);
        config.setDeviceId("E46-001");
        config.setPassword("not-a-real-credential");
        config.setEnabled(true);
    }

    CloudConfig reloaded;
    QCOMPARE(reloaded.brokerHost(), QStringLiteral("telemetry.example.com"));
    QCOMPARE(reloaded.brokerPort(), 8883);
    QCOMPARE(reloaded.deviceId(), QStringLiteral("E46-001"));
    QVERIFY(reloaded.hasPassword());
    QVERIFY(reloaded.enabled());
    QVERIFY(reloaded.configured());
}

/**
 * The regression that matters most here. QSettings creates files with the
 * process umask — 0022 on the Pi — which would leave a live broker password
 * world-readable on a machine several people may have shell access to.
 */
void TestCloudConfig::configFileIsOwnerReadWriteOnly()
{
    CloudConfig config;
    config.setBrokerHost("telemetry.example.com");
    config.setDeviceId("E46-001");
    config.setPassword("not-a-real-credential");

    QVERIFY(QFile::exists(confPath()));
    const QFileDevice::Permissions perms = QFile(confPath()).permissions();

    QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
    QVERIFY(perms.testFlag(QFileDevice::WriteOwner));

    QVERIFY(!perms.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!perms.testFlag(QFileDevice::WriteGroup));
    QVERIFY(!perms.testFlag(QFileDevice::ReadOther));
    QVERIFY(!perms.testFlag(QFileDevice::WriteOther));
}

/**
 * persist() serialises through QSettings before copying the bytes into the 0600
 * file, and QSettings is exactly the thing the 0600 file exists to avoid: it
 * writes with the process umask. That staging copy used to be a sibling of
 * cloud.conf at the fixed path cloud.conf.build, so every setter call recreated
 * a world-readable file containing the live broker password — and it was removed
 * only on the success path, so an early return or a power cut left it there.
 *
 * This asserts the outcome rather than the mechanism: whatever persist() does
 * internally, the data directory must hold cloud.conf and nothing else derived
 * from it.
 */
void TestCloudConfig::persistLeavesNoStagingCopyBesideTheConfig()
{
    CloudConfig config;
    config.setBrokerHost("telemetry.example.com");
    config.setDeviceId("E46-001");
    config.setPassword("not-a-real-credential");

    const QFileInfo conf(confPath());
    const QDir dir = conf.absoluteDir();

    const QStringList strays = dir.entryList(
        { conf.fileName() + QStringLiteral("*") }, QDir::Files);

    QCOMPARE(strays, QStringList{ conf.fileName() });
}

/**
 * LogBufferModel captures every qDebug/qInfo through a Qt message handler and
 * the Device Log settings page renders it on screen. A `password` Q_PROPERTY
 * would be one accidental `Text { text: cloudConfig.password }` away from
 * putting a live credential on a display in a car — so the property must not
 * exist at all, rather than merely not being used today.
 */
void TestCloudConfig::passwordIsNotReadableThroughTheMetaObject()
{
    CloudConfig config;
    config.setPassword("not-a-real-credential");

    const QMetaObject *meta = config.metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        const QString name = QString::fromLatin1(meta->property(i).name()).toLower();
        QVERIFY2(!name.contains("password") || name == "haspassword",
                 qPrintable(QStringLiteral("property '%1' exposes the credential to QML")
                                .arg(QString::fromLatin1(meta->property(i).name()))));
    }

    // hasPassword answers the only question the UI legitimately has.
    QCOMPARE(config.property("hasPassword").toBool(), true);
}

void TestCloudConfig::configuredRequiresHostDeviceAndPassword()
{
    CloudConfig config;
    QVERIFY(!config.configured());

    config.setBrokerHost("telemetry.example.com");
    QVERIFY(!config.configured());

    config.setDeviceId("E46-001");
    QVERIFY(!config.configured()); // still no credential — must not look ready

    config.setPassword("not-a-real-credential");
    QVERIFY(config.configured());
}

void TestCloudConfig::clearPasswordUnpairsWithoutLosingTheBroker()
{
    CloudConfig config;
    config.setBrokerHost("telemetry.example.com");
    config.setDeviceId("E46-001");
    config.setPassword("not-a-real-credential");

    config.clearPassword();

    QVERIFY(!config.hasPassword());
    QVERIFY(!config.configured());
    QCOMPARE(config.brokerHost(), QStringLiteral("telemetry.example.com"));
}

void TestCloudConfig::tlsIsOnByDefault()
{
    CloudConfig config;
    QVERIFY(config.useTls());
    QCOMPARE(config.brokerPort(), 8883);
}

QTEST_MAIN(TestCloudConfig)
#include "tst_cloudconfig.moc"

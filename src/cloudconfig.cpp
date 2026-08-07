#include "cloudconfig.h"
#include "apppaths.h"

#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
constexpr int kSchemaVersion = 1;
}

QString CloudConfig::configPath()
{
    return AppPaths::dataFile("cloud.conf");
}

CloudConfig::CloudConfig(QObject *parent)
    : QObject(parent)
{
    load();
}

bool CloudConfig::configured() const
{
    return !m_brokerHost.isEmpty() && !m_deviceId.isEmpty() && !m_password.isEmpty();
}

void CloudConfig::load()
{
    QSettings s(configPath(), QSettings::IniFormat);

    m_brokerHost = s.value("Broker/Host").toString();
    m_brokerPort = s.value("Broker/Port", 8883).toInt();
    m_useTls     = s.value("Broker/UseTls", true).toBool();
    m_caFile     = s.value("Broker/CaFile").toString();
    m_deviceId   = s.value("Device/Id").toString();
    m_enabled    = s.value("Device/Enabled", false).toBool();
    m_password   = s.value("Device/Password").toString();

    // Not logged, not even at debug, and not even redacted: the safest thing to
    // say about the credential is nothing. Whether one exists is visible in the
    // Settings UI through hasPassword().
}

void CloudConfig::persist()
{
    // QSaveFile + explicit 0600, rather than QSettings writing the path
    // directly. Two reasons:
    //
    //  1. QSettings creates the file with the process umask, which on the Pi
    //     is 0022 — a world-readable file containing a live broker password.
    //     There is no QSettings API to set the mode, and chmod'ing after the
    //     fact leaves a window where it is readable.
    //  2. QSaveFile is atomic: a power cut mid-write (entirely plausible — this
    //     runs in a car, off the ignition) leaves the previous file intact
    //     rather than a truncated one that would strand the car unpaired.
    //
    // So: serialise through QSettings, then move the bytes across under a 0600
    // QSaveFile.
    //
    // The staging file goes in a QTemporaryDir, NOT next to cloud.conf. Reason 1
    // above applies just as much to the staging copy: it is QSettings writing a
    // file, so it too lands at 0644 with the password in it, and it used to sit
    // at the fixed, guessable path cloud.conf.build — recreated on every setter
    // call. Worse, it was only removed on the success path, so an early return
    // or a power cut (the very case reason 2 exists for) left a world-readable
    // credential on disk for good. QTemporaryDir is mkdtemp, which POSIX
    // guarantees is 0700, so nothing else on the machine can read what is inside
    // it whatever mode QSettings picks — and its destructor removes the tree on
    // every path out of this function, including the early returns below.
    const QString path = configPath();

    QTemporaryDir staging;
    if (!staging.isValid()) {
        qWarning("cloud.conf: could not create a private staging directory");
        return;
    }
    const QString scratch = staging.filePath(QStringLiteral("cloud.conf"));

    {
        QSettings s(scratch, QSettings::IniFormat);
        s.setValue("SchemaVersion", kSchemaVersion);
        s.setValue("Broker/Host",     m_brokerHost);
        s.setValue("Broker/Port",     m_brokerPort);
        s.setValue("Broker/UseTls",   m_useTls);
        s.setValue("Broker/CaFile",   m_caFile);
        s.setValue("Device/Id",       m_deviceId);
        s.setValue("Device/Enabled",  m_enabled);
        s.setValue("Device/Password", m_password);
        s.sync();
    }

    QFile in(scratch);
    if (!in.open(QIODevice::ReadOnly)) {
        qWarning("cloud.conf: could not stage config for writing");
        return;
    }
    const QByteArray bytes = in.readAll();
    in.close();

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qWarning("cloud.conf: could not open for writing");
        return;
    }
    // Set before commit(): QSaveFile writes through a temporary, so permissions
    // applied afterwards would race the rename.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(bytes);
    if (!out.commit())
        qWarning("cloud.conf: write failed");
}

void CloudConfig::setBrokerHost(const QString &host)
{
    if (m_brokerHost == host)
        return;
    m_brokerHost = host;
    persist();
    emit changed();
}

void CloudConfig::setBrokerPort(int port)
{
    if (m_brokerPort == port || port <= 0 || port > 65535)
        return;
    m_brokerPort = port;
    persist();
    emit changed();
}

void CloudConfig::setDeviceId(const QString &id)
{
    if (m_deviceId == id)
        return;
    m_deviceId = id;
    persist();
    emit changed();
}

void CloudConfig::setUseTls(bool on)
{
    if (m_useTls == on)
        return;
    // Not blocked — a developer bench-testing against a local broker on 1883
    // needs it — but it is never the default, and MosquittoCloudClient warns on
    // every connect while it is off. See the plan: "a password over plaintext
    // MQTT across LTE is a leaked credential".
    m_useTls = on;
    persist();
    emit changed();
}

void CloudConfig::setCaFile(const QString &path)
{
    if (m_caFile == path)
        return;
    m_caFile = path;
    persist();
    emit changed();
}

void CloudConfig::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    persist();
    emit changed();
}

void CloudConfig::setPassword(const QString &password)
{
    if (m_password == password)
        return;
    m_password = password;
    persist();
    emit changed();
}

void CloudConfig::clearPassword()
{
    setPassword(QString());
}

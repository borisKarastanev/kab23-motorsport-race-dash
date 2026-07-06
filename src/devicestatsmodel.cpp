#include "devicestatsmodel.h"
#include "logging.h"

#include <QFile>
#include <QTextStream>
#include <QThread>
#include <QStorageInfo>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QStandardPaths>

namespace {
constexpr int kPollIntervalMs = 2000;
}

DeviceStatsModel::DeviceStatsModel(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &DeviceStatsModel::poll);
}

void DeviceStatsModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();

    if (m_active) {
        m_slowTick = 0; // refresh the slow stats immediately on (re)entry
        poll();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

void DeviceStatsModel::poll()
{
    pollCpuTemp();
    pollCpuLoad();
    pollMemory();
    pollUptime();

    if (m_slowTick == 0) {
        pollDisk();
        pollIpAddress();
        pollThrottle();
    }
    m_slowTick = (m_slowTick + 1) % kSlowPollDivisor;

    emit statsChanged();
}

void DeviceStatsModel::pollCpuTemp()
{
    QFile f("/sys/class/thermal/thermal_zone0/temp");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_cpuTempC = -1.0;
        return;
    }
    bool ok = false;
    const qint64 milliC = f.readAll().trimmed().toLongLong(&ok);
    m_cpuTempC = ok ? milliC / 1000.0 : -1.0;
}

void DeviceStatsModel::pollCpuLoad()
{
    QFile f("/proc/loadavg");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_cpuLoadPct = -1.0;
        return;
    }
    const QStringList parts = QString::fromUtf8(f.readAll()).split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        m_cpuLoadPct = -1.0;
        return;
    }
    bool ok = false;
    const double load1min = parts.first().toDouble(&ok);
    const int cores = qMax(1, QThread::idealThreadCount());
    m_cpuLoadPct = ok ? (load1min / cores) * 100.0 : -1.0;
}

void DeviceStatsModel::pollMemory()
{
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_memTotalMb = -1;
        m_memUsedMb  = -1;
        return;
    }

    qint64 totalKb = -1, availKb = -1;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith("MemTotal:"))
            totalKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
        else if (line.startsWith("MemAvailable:"))
            availKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
    }

    if (totalKb < 0 || availKb < 0) {
        m_memTotalMb = -1;
        m_memUsedMb  = -1;
        return;
    }
    m_memTotalMb = static_cast<int>(totalKb / 1024);
    m_memUsedMb  = static_cast<int>((totalKb - availKb) / 1024);
}

void DeviceStatsModel::pollDisk()
{
    const QStorageInfo info = QStorageInfo::root();
    if (!info.isValid()) {
        m_diskFreeGb  = -1.0;
        m_diskTotalGb = -1.0;
        return;
    }
    m_diskFreeGb  = info.bytesAvailable() / 1.0e9;
    m_diskTotalGb = info.bytesTotal() / 1.0e9;
}

void DeviceStatsModel::pollUptime()
{
    QFile f("/proc/uptime");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_uptimeText = "N/A";
        return;
    }
    const QStringList parts = QString::fromUtf8(f.readAll()).split(' ', Qt::SkipEmptyParts);
    bool ok = false;
    const double seconds = parts.value(0).toDouble(&ok);
    if (!ok) {
        m_uptimeText = "N/A";
        return;
    }

    const qint64 total = static_cast<qint64>(seconds);
    const qint64 days  = total / 86400;
    const qint64 hours = (total % 86400) / 3600;
    const qint64 mins  = (total % 3600) / 60;
    const qint64 secs  = total % 60;

    m_uptimeText = QStringLiteral("%1d %2:%3:%4")
        .arg(days)
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins,  2, 10, QChar('0'))
        .arg(secs,  2, 10, QChar('0'));
}

void DeviceStatsModel::pollIpAddress()
{
    for (const QHostAddress &addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        if (addr.isLoopback())
            continue;
        m_ipAddress = addr.toString();
        return;
    }
    m_ipAddress = "N/A";
}

void DeviceStatsModel::pollThrottle()
{
    if (!m_vcgencmdSearched) {
        m_vcgencmdSearched = true;
        m_vcgencmdPath = QStandardPaths::findExecutable("vcgencmd");
        if (m_vcgencmdPath.isEmpty())
            qCInfo(lcApp) << "vcgencmd not found — throttle stats unavailable (expected off-Pi)";
    }

    if (m_vcgencmdPath.isEmpty()) {
        m_throttleText = "N/A";
        return;
    }

    if (!m_throttleProc) {
        m_throttleProc = new QProcess(this);
        connect(m_throttleProc, &QProcess::finished,
                this, &DeviceStatsModel::onThrottleProcessFinished);
    }

    if (m_throttleProc->state() != QProcess::NotRunning)
        return; // previous query still in flight; keep last known value

    m_throttleProc->start(m_vcgencmdPath, { "get_throttled" });
}

void DeviceStatsModel::onThrottleProcessFinished()
{
    const QByteArray out = m_throttleProc->readAllStandardOutput().trimmed();
    // Expected form: "throttled=0x50000"
    const int eq = out.indexOf('=');
    if (eq < 0) {
        m_throttleText = "N/A";
        emit statsChanged();
        return;
    }

    bool ok = false;
    const quint32 bits = out.mid(eq + 1).toUInt(&ok, 16);
    if (!ok) {
        m_throttleText = "N/A";
        emit statsChanged();
        return;
    }

    if (bits == 0) {
        m_throttleText = "OK";
        emit statsChanged();
        return;
    }

    QStringList flags;
    if (bits & (1u << 0))  flags << "under-voltage";
    if (bits & (1u << 1))  flags << "freq capped";
    if (bits & (1u << 2))  flags << "throttled";
    if (bits & (1u << 3))  flags << "soft temp limit";
    if (bits & (1u << 16)) flags << "under-voltage occurred";
    if (bits & (1u << 17)) flags << "freq capped occurred";
    if (bits & (1u << 18)) flags << "throttled occurred";
    if (bits & (1u << 19)) flags << "soft temp limit occurred";

    m_throttleText = flags.isEmpty() ? "OK" : flags.join(", ");
    emit statsChanged();
}

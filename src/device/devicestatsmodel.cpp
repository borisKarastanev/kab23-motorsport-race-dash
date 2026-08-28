#include "src/device/devicestatsmodel.h"
#include "src/core/logging.h"

#include <QFile>
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
        m_haveLastCpuSample = false; // avoid diffing /proc/stat across the closed period
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
    // Instantaneous CPU busy % from /proc/stat's aggregate "cpu" line, as a
    // delta between this poll and the last one. Previously this read
    // /proc/loadavg's 1-minute average instead — a trailing, exponentially
    // smoothed count of runnable+I/O-blocked processes, not CPU-busy time, so
    // it could sit well above real usage (inflated by brief periodic I/O:
    // nmcli polling, vcgencmd, log flushes) and lag real load by design.
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_cpuLoadPct = -1.0;
        return;
    }
    const QStringList parts = QString::fromUtf8(f.readLine()).split(' ', Qt::SkipEmptyParts);
    // "cpu  <user> <nice> <system> <idle> <iowait> <irq> <softirq> ..." — already
    // aggregated across all cores by the kernel, so no core-count normalization
    // is needed (unlike the old loadavg-based computation).
    if (parts.size() < 6 || parts.first() != QLatin1String("cpu")) {
        m_cpuLoadPct = -1.0;
        return;
    }

    quint64 total = 0;
    for (int i = 1; i < parts.size(); ++i)
        total += parts.at(i).toULongLong();
    const quint64 idle = parts.at(4).toULongLong() + parts.at(5).toULongLong(); // idle + iowait

    if (m_haveLastCpuSample) {
        const quint64 totalDelta = total - m_lastCpuTotal;
        const quint64 idleDelta  = idle  - m_lastCpuIdle;
        m_cpuLoadPct = totalDelta > 0 ? (1.0 - double(idleDelta) / double(totalDelta)) * 100.0 : 0.0;
    } else {
        m_cpuLoadPct = -1.0; // no prior sample yet — first tick after (re)activation
    }

    m_lastCpuTotal = total;
    m_lastCpuIdle  = idle;
    m_haveLastCpuSample = true;
}

void DeviceStatsModel::pollMemory()
{
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_memTotalMb = -1;
        m_memUsedMb  = -1;
        return;
    }

    // /proc pseudo-files report size 0 via fstat(); QTextStream's atEnd() can
    // rely on that reported size and return true before any bytes are read,
    // silently yielding zero lines. QFile::readAll() bypasses that and reads
    // the real content directly — the same pattern already used successfully
    // by pollCpuTemp()/pollUptime() elsewhere in this file.
    qint64 totalKb = -1, availKb = -1, freeKb = -1, buffersKb = -1, cachedKb = -1;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &lineBytes : lines) {
        const QString line = QString::fromUtf8(lineBytes);
        if (line.startsWith("MemTotal:"))
            totalKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
        else if (line.startsWith("MemAvailable:"))
            availKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
        else if (line.startsWith("MemFree:"))
            freeKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
        else if (line.startsWith("Buffers:"))
            buffersKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
        else if (line.startsWith("Cached:"))
            cachedKb = line.split(' ', Qt::SkipEmptyParts).value(1).toLongLong();
    }

    // MemAvailable (kernel >= 3.14) is the accurate reclaimable-aware figure;
    // without it the row was stuck permanently on the "—" placeholder with no
    // way to recover. Fall back to the pre-3.14 approximation instead.
    if (availKb < 0 && freeKb >= 0 && buffersKb >= 0 && cachedKb >= 0)
        availKb = freeKb + buffersKb + cachedKb;

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

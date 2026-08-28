#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QProcess>

// Raspberry Pi device stats for the Device Info > Device Stats settings page.
// Polling only runs while the page is open (active == true) to avoid wasted
// work while racing. Every source degrades gracefully to a sentinel value
// (-1 / "N/A") when unavailable, e.g. when running on a non-Pi dev machine.
class DeviceStatsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool    active      READ active      WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(double  cpuTempC    READ cpuTempC    NOTIFY statsChanged)
    Q_PROPERTY(double  cpuLoadPct  READ cpuLoadPct  NOTIFY statsChanged)
    Q_PROPERTY(int     memUsedMb   READ memUsedMb   NOTIFY statsChanged)
    Q_PROPERTY(int     memTotalMb  READ memTotalMb  NOTIFY statsChanged)
    Q_PROPERTY(double  diskFreeGb  READ diskFreeGb  NOTIFY statsChanged)
    Q_PROPERTY(double  diskTotalGb READ diskTotalGb NOTIFY statsChanged)
    Q_PROPERTY(QString uptimeText  READ uptimeText  NOTIFY statsChanged)
    Q_PROPERTY(QString ipAddress   READ ipAddress   NOTIFY statsChanged)
    Q_PROPERTY(QString throttleText READ throttleText NOTIFY statsChanged)

public:
    explicit DeviceStatsModel(QObject *parent = nullptr);

    bool active() const { return m_active; }
    void setActive(bool active);

    double  cpuTempC()    const { return m_cpuTempC; }
    double  cpuLoadPct()  const { return m_cpuLoadPct; }
    int     memUsedMb()   const { return m_memUsedMb; }
    int     memTotalMb()  const { return m_memTotalMb; }
    double  diskFreeGb()  const { return m_diskFreeGb; }
    double  diskTotalGb() const { return m_diskTotalGb; }
    QString uptimeText()  const { return m_uptimeText; }
    QString ipAddress()   const { return m_ipAddress; }
    QString throttleText() const { return m_throttleText; }

signals:
    void activeChanged();
    void statsChanged();

private slots:
    void poll();
    void onThrottleProcessFinished();

private:
    void pollCpuTemp();
    void pollCpuLoad();
    void pollMemory();
    void pollDisk();
    void pollUptime();
    void pollIpAddress();
    void pollThrottle();

    bool m_active = false;
    QTimer m_timer;

    double  m_cpuTempC    = -1.0;
    double  m_cpuLoadPct  = -1.0;
    int     m_memUsedMb   = -1;
    int     m_memTotalMb  = -1;
    double  m_diskFreeGb  = -1.0;
    double  m_diskTotalGb = -1.0;
    QString m_uptimeText  = "N/A";
    QString m_ipAddress   = "N/A";
    QString m_throttleText = "N/A";

    // Disk, IP and throttle change slowly and are the most expensive to read
    // (statvfs, interface enumeration, a vcgencmd subprocess) — poll them once
    // every kSlowPollDivisor ticks rather than on every 2 s stats tick.
    int m_slowTick = 0;
    static constexpr int kSlowPollDivisor = 5;

    QString m_vcgencmdPath; // empty if not found / not yet looked up
    bool m_vcgencmdSearched = false;
    QProcess *m_throttleProc = nullptr;

    // Previous /proc/stat sample for computing instantaneous CPU% as a delta
    // between polls (see pollCpuLoad()). Reset whenever the page (re)activates
    // so the first tick after reopening never diffs across the time it was
    // closed (polling stops entirely while inactive).
    quint64 m_lastCpuTotal = 0;
    quint64 m_lastCpuIdle  = 0;
    bool    m_haveLastCpuSample = false;
};

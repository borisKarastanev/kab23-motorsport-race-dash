#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QDateTime;
class RaceBoxModel;

// System date/time and timezone for Settings > Device Settings > Date & Time.
// Wraps `timedatectl` (systemd-timedated) — a polkit action, so this needs no
// sudo password modal (see install.sh's 50-race-dash-timedate.rules and the
// note in the timezone-settings plan on why the dash can't prompt for one in
// a car). Uses the shared Proc::run for its subprocess calls and CloudConfig's
// single `changed()` signal for every property.
//
// Three time sources, in order of preference: NTP (toggle on, needs internet),
// a manual region/city + date/time entry (no internet), and the RaceBox GPS
// UTC fix (no internet, no manual entry either) — see syncFromGps().
class TimeModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool syncEnabled  READ syncEnabled  NOTIFY changed)
    Q_PROPERTY(bool clockSynced  READ clockSynced  NOTIFY changed)
    Q_PROPERTY(QString timezoneId READ timezoneId  NOTIFY changed)
    Q_PROPERTY(QString utcOffset  READ utcOffset   NOTIFY changed)
    Q_PROPERTY(QString localTimeText READ localTimeText NOTIFY changed)
    Q_PROPERTY(bool online       READ online       NOTIFY changed)
    Q_PROPERTY(bool gpsTimeValid READ gpsTimeValid NOTIFY changed)
    Q_PROPERTY(QString gpsTimeText READ gpsTimeText NOTIFY changed)
    Q_PROPERTY(bool busy         READ busy         NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText    NOTIFY changed)
    // Sorted unique IANA region prefixes ("Africa", "America", …). CONSTANT
    // because QTimeZone's id list is fixed for the process lifetime.
    Q_PROPERTY(QStringList regions READ regions CONSTANT)

public:
    explicit TimeModel(bool mockMode, RaceBoxModel *raceBox, QObject *parent = nullptr);

    // Deferred off the boot path, exactly as UplinkModel::start() is: the first
    // status read spawns timedatectl (which D-Bus-activates systemd-timedated)
    // and nmcli, and first-run bootstrap adds an SD-card write. None of that
    // may sit between boot and the dashboard's first frame. main.cpp hooks this
    // to the same frameSwapped signal. Nothing reads this model's state until
    // the Date & Time page is opened, so a one-frame delay costs nothing.
    void start();

    bool       syncEnabled()   const { return m_syncEnabled; }
    bool       clockSynced()   const { return m_clockSynced; }
    QString    timezoneId()    const;
    QString    utcOffset()     const;
    QString    localTimeText() const;
    bool       online()        const { return m_online; }
    bool       gpsTimeValid()  const;
    QString    gpsTimeText()   const;
    bool       busy()          const { return m_busy; }
    QString    errorText()     const { return m_errorText; }
    QStringList regions()      const;

    // Sorted city suffixes for a region, e.g. zonesForRegion("Europe") ->
    // ["Amsterdam", "Andorra", …, "Sofia", …]. Combine as `region + "/" + zone`
    // to get back a full IANA id for setTimezone().
    Q_INVOKABLE QStringList zonesForRegion(const QString &region) const;

    // Refcounted page-open gating — see NetworkModel::acquire()/release(). A
    // StackView can keep TimeZoneDetail alive underneath the region/zone
    // picker pages, so a plain bool would clear too early.
    Q_INVOKABLE void acquire();
    Q_INVOKABLE void release();

    Q_INVOKABLE void setSyncEnabled(bool on);
    Q_INVOKABLE void setTimezone(const QString &id);
    // `second` has no field in the entry modal; syncFromGps() bypasses this
    // entirely and calls applyDateTime() with a full QDateTime.
    Q_INVOKABLE void setManualDateTime(int year, int month, int day,
                                        int hour, int minute, int second = 0);
    Q_INVOKABLE void syncFromGps();
    // Sends the device's IP to a third party (ipapi.co / ip-api.com), so it is
    // fired only by an explicit toggle-on, an explicit DETECT tap, or the
    // one-time first-run bootstrap below — never on a timer or a status poll.
    Q_INVOKABLE void detectTimezoneFromIp();

    // ── pure helpers, unit-testable against a fixed id list ──────────────
    static QStringList regionsFromIds(const QList<QByteArray> &ids);
    static QStringList zonesForRegionFromIds(const QList<QByteArray> &ids, const QString &region);
    static QString     formatUtcOffset(int offsetSeconds);
    static bool         isKnownTimezoneId(const QString &id, const QList<QByteArray> &ids);
    // Trims raw and returns it only if it names a known zone; empty otherwise.
    // The one guard between a third-party HTTP response and a subprocess arg.
    static QString      validateZoneCandidate(const QString &raw, const QList<QByteArray> &ids);

signals:
    void changed();

private slots:
    void onTick();

private:
    void maybeBootstrap();
    void refreshStatus();
    // The two halves of refreshStatus(), each taking a continuation. The
    // first-run bootstrap needs BOTH answers before it can decide, and firing
    // them in parallel (as refreshStatus does) would let maybeBootstrap() read
    // a default-false m_syncEnabled while the timedatectl query is still in
    // flight — which reads as "sync is off" on every device, enabled or not.
    void refreshNtpState(std::function<void()> next);
    void refreshConnectivity(std::function<void()> next);

    // Shared completion for the one-shot timedatectl writes: clears busy, turns
    // a failure into "<what>: <last line of output>", notifies. `what` is the
    // user-facing prefix, e.g. "Could not set timezone".
    std::function<void(bool, int, const QString &)> finisher(const QString &what);

    void applyTimezone(const QString &id);
    // The set-ntp-false -> set-time chain, shared by the manual entry modal and
    // syncFromGps(). Takes a validated LOCAL datetime.
    void applyDateTime(const QDateTime &local);

    void fetchTimezoneFrom(const QUrl &url, bool tryFallbackOnFail);
    void runTimedatectl(const QStringList &args, int timeoutMs,
                         std::function<void(bool ok, int exitCode, const QString &out)> onDone);

    static QString configPath();

    bool m_mockMode;
    RaceBoxModel *m_raceBox = nullptr;

    bool    m_syncEnabled = false;
    bool    m_clockSynced = false;
    bool    m_online      = false;
    bool    m_busy         = false;
    QString m_errorText;

    // Only touched in mock mode: a real setTimezone() shells out to
    // timedatectl and QTimeZone::systemTimeZoneId() picks it up directly, but
    // mock mode never shells out (must not touch the dev machine's real
    // clock/zone), so it needs its own place to remember what was "set".
    QString m_mockTimezoneId;

    QNetworkAccessManager *m_nam = nullptr;

    QTimer m_timer; // 1 Hz while acquired; drives the live localTimeText tick
    int    m_activeRefs  = 0;
    int    m_tickCount   = 0;
    static constexpr int kStatusRefreshEveryTicks = 5; // re-poll timedatectl/nmcli every 5 s
};

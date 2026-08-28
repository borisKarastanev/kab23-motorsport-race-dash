#include "src/device/timemodel.h"
#include "src/race/raceboxmodel.h"
#include "src/core/procrunner.h"
#include "src/core/apppaths.h"
#include "src/core/logging.h"

#include <QSettings>
#include <QDateTime>
#include <QTimeZone>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <algorithm>

namespace {

constexpr int kProcessTimeoutMs = 8000;

// QTimeZone's id list (~600 entries) is fixed for the process lifetime, so
// enumerate it once instead of on every regions() read, zone-list drill-in,
// setTimezone() validation and geolocation check. Function-local static, not
// namespace-scope: this must not be paid during startup, only on first use.
const QList<QByteArray> &allZoneIds()
{
    static const QList<QByteArray> ids = QTimeZone::availableTimeZoneIds();
    return ids;
}

}

TimeModel::TimeModel(bool mockMode, RaceBoxModel *raceBox, QObject *parent)
    : QObject(parent)
    , m_mockMode(mockMode)
    , m_raceBox(raceBox)
{
    // Cheap by contract — see start(). No subprocess, no file I/O, no running
    // timer: this is constructed before the dashboard's first frame.
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &TimeModel::onTick);
}

void TimeModel::start()
{
    if (m_mockMode) {
        m_mockTimezoneId = QString::fromUtf8(QTimeZone::systemTimeZoneId());
        // DASH_MOCK_OFFLINE lets the offline manual-entry path be exercised
        // under --mock without physically pulling the dev box's network,
        // mirroring UpdateModel's DASH_MOCK_UPDATE escape hatch.
        m_online = !qEnvironmentVariableIsSet("DASH_MOCK_OFFLINE");
        maybeBootstrap();
        return;
    }

    // Chained, not parallel: maybeBootstrap() must see the real NTP state and
    // the real connectivity, and both arrive from separate subprocesses.
    refreshNtpState([this]() {
        refreshConnectivity([this]() { maybeBootstrap(); });
    });
}

// ── property reads ───────────────────────────────────────────────────

QString TimeModel::timezoneId() const
{
    return m_mockMode ? m_mockTimezoneId : QString::fromUtf8(QTimeZone::systemTimeZoneId());
}

QString TimeModel::utcOffset() const
{
    // Through timezoneId() so the mock/real branch lives in exactly one place.
    const QTimeZone tz(timezoneId().toUtf8());
    if (!tz.isValid())
        return QString();
    return formatUtcOffset(tz.offsetFromUtc(QDateTime::currentDateTime()));
}

QString TimeModel::localTimeText() const
{
    // Through timezoneId(), like utcOffset(): QDateTime::currentDateTime()
    // alone reflects the OS's actual local zone, not the mock zone / the zone
    // the user just picked, so a plain-local read here would leave this row
    // showing the wrong time after setTimezone() in mock mode.
    const QTimeZone tz(timezoneId().toUtf8());
    const QDateTime now = tz.isValid()
        ? QDateTime::currentDateTimeUtc().toTimeZone(tz)
        : QDateTime::currentDateTime();
    return now.toString("yyyy-MM-dd HH:mm:ss");
}

bool TimeModel::gpsTimeValid() const
{
    return m_raceBox && m_raceBox->gpsTimeValid();
}

QString TimeModel::gpsTimeText() const
{
    if (!gpsTimeValid())
        return QString();
    // Explicit pattern, not Qt::ISODate: that already appends a "Z" on a UTC
    // datetime, which would render as "…T14:03:11Z UTC". Matches localTimeText.
    return m_raceBox->gpsUtc().toString("yyyy-MM-dd HH:mm:ss") + " UTC";
}

QStringList TimeModel::regions() const
{
    return regionsFromIds(allZoneIds());
}

QStringList TimeModel::zonesForRegion(const QString &region) const
{
    return zonesForRegionFromIds(allZoneIds(), region);
}

// ── page-open gating ─────────────────────────────────────────────────

void TimeModel::acquire()
{
    m_activeRefs++;
    if (m_activeRefs == 1) {
        m_tickCount = 0;
        m_timer.start();
        refreshStatus();
    }
}

void TimeModel::release()
{
    if (m_activeRefs > 0)
        m_activeRefs--;
    if (m_activeRefs == 0)
        m_timer.stop();
}

void TimeModel::onTick()
{
    emit changed(); // localTimeText / gpsTimeText are live reads — just re-notify
    if (++m_tickCount % kStatusRefreshEveryTicks == 0)
        refreshStatus();
}

// ── Q_INVOKABLE actions ──────────────────────────────────────────────

std::function<void(bool, int, const QString &)> TimeModel::finisher(const QString &what)
{
    return [this, what](bool ok, int, const QString &out) {
        m_busy = false;
        m_errorText = ok ? QString() : (what + ": " + Proc::lastNonEmptyLine(out));
        emit changed();
    };
}

void TimeModel::setSyncEnabled(bool on)
{
    if (m_busy)
        return;

    if (m_mockMode) {
        qCInfo(lcApp) << "[mock] set-ntp" << on;
        m_syncEnabled = on;
        m_clockSynced = on;
        m_errorText.clear();
        emit changed();
        if (on && m_online)
            detectTimezoneFromIp();
        return;
    }

    m_busy = true;
    emit changed();
    runTimedatectl({"set-ntp", on ? "true" : "false"}, kProcessTimeoutMs,
        [this, on](bool ok, int, const QString &out) {
            m_busy = false;
            if (!ok) {
                m_errorText = "Could not change sync: " + Proc::lastNonEmptyLine(out);
                emit changed();
                return;
            }
            m_errorText.clear();
            // Only the NTP state can have changed here — no need to re-spawn
            // nmcli for connectivity as well.
            refreshNtpState({});
            emit changed();
            if (on && m_online)
                detectTimezoneFromIp();
        });
}

void TimeModel::setTimezone(const QString &id)
{
    if (m_busy)
        return;

    if (!isKnownTimezoneId(id, allZoneIds())) {
        m_errorText = "Unknown timezone: " + id;
        emit changed();
        return;
    }

    m_busy = true;
    m_errorText.clear();
    emit changed();
    applyTimezone(id);
}

void TimeModel::applyTimezone(const QString &id)
{
    // Mock-aware here (not just in setTimezone()), because detectTimezoneFromIp()
    // also funnels its result through this same function on success — and that
    // path must stay usable under --mock (see detectTimezoneFromIp()) without
    // shelling out to the real timedatectl on this machine.
    if (m_mockMode) {
        qCInfo(lcApp) << "[mock] set-timezone" << id;
        m_mockTimezoneId = id;
        m_busy = false;
        m_errorText.clear();
        emit changed();
        return;
    }

    runTimedatectl({"set-timezone", id}, kProcessTimeoutMs, finisher("Could not set timezone"));
}

void TimeModel::setManualDateTime(int year, int month, int day,
                                   int hour, int minute, int second)
{
    // Validate BEFORE touching NTP. applyDateTime() reaches set-time only after
    // set-ntp false has already succeeded, so letting an impossible date (31
    // February, a 60th leap second) through would leave the device with sync
    // disabled AND the clock unchanged — strictly worse than not tapping.
    const QDateTime candidate(QDate(year, month, day), QTime(hour, minute, second));
    if (!candidate.isValid()) {
        m_errorText = "Invalid date or time";
        emit changed();
        return;
    }
    applyDateTime(candidate);
}

void TimeModel::syncFromGps()
{
    if (!gpsTimeValid())
        return;
    // Straight through as a QDateTime — no destructure/rebuild round trip, so
    // the GPS seconds survive. Rounding them to :00 would set the clock up to
    // a minute slow, defeating the point of an accurate offline source.
    applyDateTime(m_raceBox->gpsUtc().toLocalTime());
}

void TimeModel::applyDateTime(const QDateTime &local)
{
    if (m_busy || !local.isValid())
        return;

    const QString stamp = local.toString("yyyy-MM-dd HH:mm:ss");

    if (m_mockMode) {
        qCInfo(lcApp) << "[mock] set-time" << stamp;
        m_syncEnabled = false;
        m_clockSynced = false;
        m_errorText.clear();
        emit changed();
        return;
    }

    m_busy = true;
    emit changed();
    // timedatectl set-time hard-fails with "Automatic time synchronization is
    // enabled" while NTP is on, so it must be disabled first.
    runTimedatectl({"set-ntp", "false"}, kProcessTimeoutMs,
        [this, stamp](bool ok, int, const QString &out) {
            if (!ok) {
                m_busy = false;
                m_errorText = "Could not disable sync: " + Proc::lastNonEmptyLine(out);
                emit changed();
                return;
            }
            m_syncEnabled = false;
            m_clockSynced = false;
            runTimedatectl({"set-time", stamp}, kProcessTimeoutMs,
                            finisher("Could not set time"));
        });
}

void TimeModel::detectTimezoneFromIp()
{
    if (m_busy)
        return;

    // No mock short-circuit: this is a read-only HTTP GET, not a system-state
    // write (applyTimezone() is what guards against touching this machine's
    // real timezone, and it's mock-aware on its own). Skipping the request
    // here as well would leave the "SYNCHRONIZE TIMEZONE" toggle a no-op
    // under --mock.
    m_busy = true;
    m_errorText.clear();
    emit changed();

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    fetchTimezoneFrom(QUrl("https://ipapi.co/timezone"), /*tryFallbackOnFail=*/true);
}

void TimeModel::fetchTimezoneFrom(const QUrl &url, bool tryFallbackOnFail)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, tryFallbackOnFail]() {
        reply->deleteLater();
        const QString body = QString::fromUtf8(reply->readAll());
        const QString zone = validateZoneCandidate(body, allZoneIds());

        if (reply->error() == QNetworkReply::NoError && !zone.isEmpty()) {
            applyTimezone(zone);
            return;
        }

        if (tryFallbackOnFail) {
            fetchTimezoneFrom(QUrl("http://ip-api.com/line/?fields=timezone"), /*tryFallbackOnFail=*/false);
            return;
        }

        m_busy = false;
        m_errorText = "Could not detect timezone from IP";
        emit changed();
    });
}

// ── first-run bootstrap ──────────────────────────────────────────────

void TimeModel::maybeBootstrap()
{
    QSettings s(configPath(), QSettings::IniFormat);
    if (s.value("Time/Bootstrapped", false).toBool())
        return;

    // Offline: leave the flag UNWRITTEN and try again next boot. A dash first
    // powered up in a car with no Wi-Fi — the normal case — would otherwise
    // burn its one bootstrap here and never auto-enable sync afterwards.
    if (!m_online)
        return;

    s.setValue("Time/Bootstrapped", true);
    s.sync();

    // Already on (m_syncEnabled is a real reading by now — see the chained
    // reads in start()): nothing to bootstrap, and re-detecting the zone would
    // silently replace a hand-picked one.
    //
    // Otherwise turn sync on, for this first online run only, so it can't fight
    // a user who later turns it off. The toggle itself always reflects the
    // actual NTP state, never a stored preference.
    if (!m_syncEnabled)
        setSyncEnabled(true);
}

QString TimeModel::configPath()
{
    return AppPaths::dataFile("time.conf");
}

// ── real-mode status refresh ─────────────────────────────────────────

void TimeModel::refreshNtpState(std::function<void()> next)
{
    runTimedatectl({"show", "-p", "NTP", "-p", "NTPSynchronized", "--value"}, kProcessTimeoutMs,
        [this, next = std::move(next)](bool ok, int, const QString &out) {
            const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
            if (ok && lines.size() >= 2) {
                m_syncEnabled = lines[0].trimmed().compare("yes", Qt::CaseInsensitive) == 0;
                m_clockSynced = lines[1].trimmed().compare("yes", Qt::CaseInsensitive) == 0;
                emit changed();
            }
            if (next)
                next();
        });
}

void TimeModel::refreshConnectivity(std::function<void()> next)
{
    Proc::run(this, QStringLiteral("nmcli"), {"-t", "-f", "CONNECTIVITY", "general", "status"},
        kProcessTimeoutMs,
        [this, next = std::move(next)](bool ok, int, const QString &out) {
            const bool nowOnline = ok && out.trimmed().compare("full", Qt::CaseInsensitive) == 0;
            if (nowOnline != m_online) {
                m_online = nowOnline;
                emit changed();
            }
            if (next)
                next();
        });
}

void TimeModel::refreshStatus()
{
    if (m_mockMode)
        return;
    // Parallel is fine here: nothing downstream needs the two answers together.
    refreshNtpState({});
    refreshConnectivity({});
}

void TimeModel::runTimedatectl(const QStringList &args, int timeoutMs,
                                std::function<void(bool ok, int exitCode, const QString &out)> onDone)
{
    Proc::run(this, QStringLiteral("timedatectl"), args, timeoutMs, std::move(onDone));
}

// ── pure helpers ──────────────────────────────────────────────────────

QStringList TimeModel::regionsFromIds(const QList<QByteArray> &ids)
{
    QSet<QString> set;
    for (const QByteArray &id : ids) {
        const QString s = QString::fromUtf8(id);
        const int slash = s.indexOf('/');
        if (slash < 0)
            continue; // e.g. "UTC" — no region, doesn't fit the drill-in idiom
        set.insert(s.left(slash));
    }
    QStringList list(set.constBegin(), set.constEnd());
    list.sort();
    return list;
}

QStringList TimeModel::zonesForRegionFromIds(const QList<QByteArray> &ids, const QString &region)
{
    const QString prefix = region + '/';
    QStringList list;
    for (const QByteArray &id : ids) {
        const QString s = QString::fromUtf8(id);
        if (s.startsWith(prefix))
            list << s.mid(prefix.length());
    }
    list.sort();
    return list;
}

QString TimeModel::formatUtcOffset(int offsetSeconds)
{
    const char sign = offsetSeconds < 0 ? '-' : '+';
    const int absSeconds = std::abs(offsetSeconds);
    return QStringLiteral("%1%2:%3").arg(QChar(sign))
        .arg(absSeconds / 3600, 2, 10, QChar('0'))
        .arg((absSeconds % 3600) / 60, 2, 10, QChar('0'));
}

bool TimeModel::isKnownTimezoneId(const QString &id, const QList<QByteArray> &ids)
{
    return ids.contains(id.toUtf8());
}

QString TimeModel::validateZoneCandidate(const QString &raw, const QList<QByteArray> &ids)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty() || !isKnownTimezoneId(trimmed, ids))
        return QString();
    return trimmed;
}

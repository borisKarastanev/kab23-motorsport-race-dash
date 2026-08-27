#include "sessionmodel.h"
#include "candatamodel.h"
#include "raceboxmodel.h"
#include "trackmodel.h"
#include "logging.h"
#include "apppaths.h"
#include "optimallap.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

namespace {
// Max speed (km/h) at which a session may be saved — must be stationary/crawling
// so the driver isn't tapping SAVE mid-lap.
constexpr int kSaveMaxSpeedKmh = 5;
}

QString SessionModel::sessionsPath()
{
    return AppPaths::dataFile("sessions.json");
}

SessionModel::SessionModel(CanDataModel *canModel, RaceBoxModel *raceBoxModel,
                           TrackModel *trackModel, QObject *parent)
    : QObject(parent)
    , m_canModel(canModel)
    , m_raceBoxModel(raceBoxModel)
    , m_trackModel(trackModel)
{
    connect(m_raceBoxModel, &RaceBoxModel::lapCompleted,
            this, &SessionModel::onLapCompleted);
    connect(m_raceBoxModel, &RaceBoxModel::lapSectorsCompleted,
            this, &SessionModel::onLapSectorsCompleted);
    connect(m_canModel, &CanDataModel::speedChanged,
            this, &SessionModel::onSpeedChanged);
    connect(m_canModel, &CanDataModel::oilTempChanged,
            this, &SessionModel::onOilTempChanged);
    connect(m_canModel, &CanDataModel::coolantTempChanged,
            this, &SessionModel::onCoolantTempChanged);

    load();
}

void SessionModel::onLapCompleted(qint64 ms, const QVariantList &path)
{
    if (ms <= 0)
        return;
    const bool wasEmpty = m_currentLapTimes.isEmpty();
    m_currentLapTimes.append(ms);
    m_currentLapPaths.append(path);
    qCInfo(lcApp) << "[SessionModel] lap appended —" << ms << "ms | total laps:" << m_currentLapTimes.size();
    if (wasEmpty) {
        emit hasLapsChanged();
        updateCanSave();
    }
}

void SessionModel::onLapSectorsCompleted(const QList<qint64> &sectorMs)
{
    // Emitted by RaceBoxModel immediately alongside lapCompleted, for the same
    // lap, so this list's size always tracks m_currentLapTimes's. Read only at
    // save time (see saveCurrentSession()) — nothing needs it live.
    //
    // Enforce that pairing rather than assume it: onLapCompleted() rejects
    // non-positive lap times, and nothing stops a future emitter of
    // lapCompleted from doing likewise. An unpaired append here would shift
    // every later lap's splits onto the wrong lap number (the 1-based index
    // into this list *is* the lap number, see saveCurrentSession()) silently,
    // with no assertion to reveal it.
    if (m_currentLapSectorTimes.size() >= m_currentLapTimes.size()) {
        qCWarning(lcApp) << "[SessionModel] sector splits with no matching lap — discarding"
                         << "| laps:" << m_currentLapTimes.size()
                         << "| split lists:" << m_currentLapSectorTimes.size();
        return;
    }
    m_currentLapSectorTimes.append(sectorMs);
}

void SessionModel::onSpeedChanged()
{
    const int spd = m_canModel->speed();
    if (spd > m_topSpeedKmh)
        m_topSpeedKmh = spd;
    updateCanSave();
}

void SessionModel::updateCanSave()
{
    const bool v = hasLaps() && m_canModel->speed() <= kSaveMaxSpeedKmh;
    if (v != m_canSave) {
        m_canSave = v;
        emit canSaveChanged();
    }
}

void SessionModel::onOilTempChanged()
{
    const double t = m_canModel->oilTemp();
    if (t > m_maxOilC)
        m_maxOilC = t;
}

void SessionModel::onCoolantTempChanged()
{
    const double t = m_canModel->coolantTemp();
    if (t > m_maxCoolantC)
        m_maxCoolantC = t;
}

void SessionModel::saveCurrentSession()
{
    if (m_currentLapTimes.isEmpty())
        return;

    const QDateTime now = QDateTime::currentDateTime();

    QJsonArray lapArray;
    for (qint64 ms : m_currentLapTimes)
        lapArray.append(ms);

    QJsonArray pathsArray;
    for (const QVariantList &path : m_currentLapPaths) {
        QJsonArray pointArray;
        for (const QVariant &v : path)
            pointArray.append(v.toDouble());
        pathsArray.append(pointArray);
    }

    // The best-sector-stitch optimal lap over this now-completing session's
    // laps (~/development/optimal-lap-algorithm.md) — a property of the
    // session once it's done, like topSpeedKmh, not a live dashboard readout.
    // Absent (0 / empty array) when fewer than two laps recorded a complete
    // set of sector splits; the Sessions view treats that the same as a
    // legacy session saved before sector gates existed.
    QList<OptimalLap::SectoredLap> sectoredLaps;
    sectoredLaps.reserve(m_currentLapSectorTimes.size());
    for (int i = 0; i < m_currentLapSectorTimes.size(); ++i)
        sectoredLaps.append({i + 1, m_currentLapSectorTimes[i]});
    // The sector count is a runtime property of the gates in force this session
    // (RaceBoxModel derives 2 gates → 3 sectors today, but a restored gate list
    // can be any length), so read it off the laps themselves rather than
    // leaning on compute()'s default. Hard-coding 3 against a 4-sector track
    // would make every lap ineligible and silently drop the optimal lap.
    int sectorCount = 0;
    for (const QList<qint64> &splits : m_currentLapSectorTimes) {
        if (!splits.isEmpty()) { sectorCount = splits.size(); break; }
    }
    const std::optional<OptimalLap::Result> optimal =
        OptimalLap::compute(sectoredLaps, sectorCount);

    QJsonArray optimalSectorsArray;
    if (optimal) {
        for (const OptimalLap::BestSector &sector : optimal->sectors) {
            QJsonObject sectorObj;
            sectorObj["sector"]    = sector.sector;
            sectorObj["lapNumber"] = sector.lapNumber;
            sectorObj["sectorMs"]  = sector.sectorMs;
            optimalSectorsArray.append(sectorObj);
        }
    }

    QJsonObject record;
    record["title"]         = now.toString("yyyy-MM-dd HH:mm");
    record["timestampIso"]  = now.toString(Qt::ISODate);
    record["lapMs"]         = lapArray;
    record["lapPaths"]      = pathsArray;
    record["topSpeedKmh"]   = m_topSpeedKmh;
    record["maxOilC"]       = m_maxOilC;
    record["maxCoolantC"]   = m_maxCoolantC;
    record["maxLatG"]       = m_raceBoxModel->maxLatG();
    record["maxLonG"]       = m_raceBoxModel->maxLonG();
    record["trackId"]       = m_trackModel->activeTrackId();
    record["trackName"]     = m_trackModel->activeTrackName();
    record["optimalLapMs"]  = optimal ? optimal->lapMs : 0;
    record["optimalSectors"] = optimalSectorsArray;

    // Prepend to keep newest-first order
    m_sessions.prepend(record.toVariantMap());
    rebuildSessionGroups();
    persist();
    emit sessionsChanged();

    // Reset accumulators so a fresh session can be recorded without rebooting.
    // The g-force peaks live in RaceBoxModel (it sees raw un-throttled samples)
    // but are reset here, alongside our own accumulators, so every session-peak
    // stat clears at the same point in the save.
    m_currentLapTimes.clear();
    m_currentLapPaths.clear();
    m_currentLapSectorTimes.clear();
    m_topSpeedKmh = 0;
    m_maxOilC     = 0.0;
    m_maxCoolantC = 0.0;
    m_raceBoxModel->resetSessionStats();
    emit hasLapsChanged();
    updateCanSave();

    // Let listeners (RaceBoxModel, via main.cpp) know a session was persisted
    // so lap numbering/timing can start fresh from the next finish-line
    // crossing instead of continuing to accumulate against the saved session.
    emit sessionSaved();

    qCInfo(lcApp) << "Session saved —" << record["title"].toString()
                  << "| laps:" << lapArray.size()
                  << "| top speed:" << m_topSpeedKmh << "km/h";
}

void SessionModel::deleteSession(const QString &timestampIso)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).toMap().value("timestampIso").toString() != timestampIso)
            continue;

        m_sessions.removeAt(i);
        rebuildSessionGroups();
        persist();
        emit sessionsChanged();
        qCInfo(lcApp) << "Session deleted —" << timestampIso;
        return;
    }
    qCWarning(lcApp) << "deleteSession: no session found with timestampIso" << timestampIso;
}

bool SessionModel::hasSessionsForTrack(const QString &trackId) const
{
    for (const QVariant &v : m_sessions) {
        if (v.toMap().value("trackId").toString() == trackId)
            return true;
    }
    return false;
}

void SessionModel::rebuildSessionGroups()
{
    // m_sessions is already newest-first (prepend on save); iterating in that
    // order and creating a group on first encounter keeps the group list
    // sorted by most-recent session with no explicit sort needed.
    QStringList order;
    QHash<QString, QVariantMap> groups;
    QHash<QString, QVariantList> groupSessions;

    for (const QVariant &v : m_sessions) {
        const QVariantMap session = v.toMap();
        const QString trackId = session.value("trackId").toString();
        const QString key = trackId.isEmpty() ? QStringLiteral("__unknown__") : trackId;

        if (!groups.contains(key)) {
            QVariantMap group;
            group["trackId"]     = trackId;
            group["trackName"]   = trackId.isEmpty() ? QStringLiteral("UNKNOWN")
                                                       : session.value("trackName");
            group["latestTitle"] = session.value("title");
            groups[key] = group;
            order.append(key);
        }
        groupSessions[key].append(v);
    }

    QVariantList result;
    result.reserve(order.size());
    for (const QString &key : order) {
        QVariantMap group = groups.value(key);
        group["count"]    = groupSessions.value(key).size();
        group["sessions"] = groupSessions.value(key);
        result.append(group);
    }
    m_sessionGroups = result;
}

void SessionModel::load()
{
    QFile f(sessionsPath());
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcApp) << "Could not open sessions.json for reading";
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcApp) << "sessions.json parse error:" << err.errorString();
        return;
    }

    const QJsonArray arr = doc.array();
    m_sessions.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        QVariantMap map = obj.toVariantMap();
        // Ensure lapMs is a QVariantList<int> — toVariantMap() yields qlonglong values
        const QJsonArray lapArr = obj["lapMs"].toArray();
        QVariantList laps;
        laps.reserve(lapArr.size());
        for (const QJsonValue &l : lapArr)
            laps.append(static_cast<int>(l.toInt()));
        map["lapMs"] = laps;

        // lapPaths: array of flat [lat, lon, …] arrays, parallel to lapMs; absent in
        // records saved before GPS-path capture existed
        const QJsonArray pathsArr = obj["lapPaths"].toArray();
        QVariantList lapPaths;
        lapPaths.reserve(pathsArr.size());
        for (const QJsonValue &p : pathsArr) {
            const QJsonArray pointArr = p.toArray();
            QVariantList path;
            path.reserve(pointArr.size());
            for (const QJsonValue &coord : pointArr)
                path.append(coord.toDouble());
            // QVariantList::append() is ambiguous when the argument is itself a
            // QVariantList (QList<QVariant>): overload resolution prefers the exact
            // QList<QVariant> match — which concatenates all of path's elements —
            // over the QVariant conversion that would nest path as one element.
            // Wrap explicitly so each lap's path is nested, not flattened.
            lapPaths.append(QVariant(path));
        }
        map["lapPaths"] = lapPaths;

        m_sessions.append(map);
    }

    rebuildSessionGroups();
    qCInfo(lcApp) << "Loaded" << m_sessions.size() << "session(s) from sessions.json";
}

void SessionModel::persist()
{
    QJsonArray arr;
    for (const QVariant &v : m_sessions)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));

    QFile f(sessionsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcApp) << "Could not write sessions.json";
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

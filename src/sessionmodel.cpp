#include "sessionmodel.h"
#include "candatamodel.h"
#include "raceboxmodel.h"
#include "trackmodel.h"
#include "logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

QString SessionModel::sessionsPath()
{
    return QCoreApplication::applicationDirPath() + "/sessions.json";
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
    if (wasEmpty)
        emit hasLapsChanged();
}

void SessionModel::onSpeedChanged()
{
    const int spd = m_canModel->speed();
    if (spd > m_topSpeedKmh)
        m_topSpeedKmh = spd;
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
        lapArray.append(static_cast<qint64>(ms));

    QJsonArray pathsArray;
    for (const QVariantList &path : m_currentLapPaths) {
        QJsonArray pointArray;
        for (const QVariant &v : path)
            pointArray.append(v.toDouble());
        pathsArray.append(pointArray);
    }

    QJsonObject record;
    record["title"]        = now.toString("yyyy-MM-dd HH:mm");
    record["timestampIso"] = now.toString(Qt::ISODate);
    record["lapMs"]        = lapArray;
    record["lapPaths"]     = pathsArray;
    record["topSpeedKmh"]  = m_topSpeedKmh;
    record["maxOilC"]      = m_maxOilC;
    record["maxCoolantC"]  = m_maxCoolantC;
    record["trackId"]      = m_trackModel->activeTrackId();
    record["trackName"]    = m_trackModel->activeTrackName();

    // Prepend to keep newest-first order
    m_sessions.prepend(record.toVariantMap());
    persist();
    emit sessionsChanged();

    // Reset accumulators so a fresh session can be recorded without rebooting
    m_currentLapTimes.clear();
    m_currentLapPaths.clear();
    m_topSpeedKmh = 0;
    m_maxOilC     = 0.0;
    m_maxCoolantC = 0.0;
    emit hasLapsChanged();

    // Let listeners (RaceBoxModel, via main.cpp) know a session was persisted
    // so lap numbering/timing can start fresh from the next finish-line
    // crossing instead of continuing to accumulate against the saved session.
    emit sessionSaved();

    qCInfo(lcApp) << "Session saved —" << record["title"].toString()
                  << "| laps:" << lapArray.size()
                  << "| top speed:" << m_topSpeedKmh << "km/h";
}

QVariantList SessionModel::sessionGroups() const
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
    return result;
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
            lapPaths.append(path);
        }
        map["lapPaths"] = lapPaths;

        m_sessions.append(map);
    }

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

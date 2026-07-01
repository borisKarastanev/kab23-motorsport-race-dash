#include "sessionmodel.h"
#include "candatamodel.h"
#include "raceboxmodel.h"
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

SessionModel::SessionModel(CanDataModel *canModel, RaceBoxModel *raceBoxModel, QObject *parent)
    : QObject(parent)
    , m_canModel(canModel)
    , m_raceBoxModel(raceBoxModel)
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

void SessionModel::onLapCompleted(qint64 ms)
{
    if (ms <= 0)
        return;
    const bool wasEmpty = m_currentLapTimes.isEmpty();
    m_currentLapTimes.append(ms);
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

    QJsonObject record;
    record["title"]        = now.toString("yyyy-MM-dd HH:mm");
    record["timestampIso"] = now.toString(Qt::ISODate);
    record["lapMs"]        = lapArray;
    record["topSpeedKmh"]  = m_topSpeedKmh;
    record["maxOilC"]      = m_maxOilC;
    record["maxCoolantC"]  = m_maxCoolantC;

    // Prepend to keep newest-first order
    m_sessions.prepend(record.toVariantMap());
    persist();
    emit sessionsChanged();

    // Reset accumulators so a fresh session can be recorded without rebooting
    m_currentLapTimes.clear();
    m_topSpeedKmh = 0;
    m_maxOilC     = 0.0;
    m_maxCoolantC = 0.0;
    emit hasLapsChanged();

    qCInfo(lcApp) << "Session saved —" << record["title"].toString()
                  << "| laps:" << lapArray.size()
                  << "| top speed:" << m_topSpeedKmh << "km/h";
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

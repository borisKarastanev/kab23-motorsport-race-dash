#include "trackmodel.h"
#include "raceboxmodel.h"
#include "logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>

namespace {
constexpr double kDetectRadiusM = 5000.0;
constexpr int    kDetectIntervalMs = 5000;
constexpr double kDefaultFinishRadiusM = 20.0;
const char *kMockTrackId = "mock-track";
}

QString TrackModel::userStatePath()
{
    return QCoreApplication::applicationDirPath() + "/tracks-user.json";
}

QString TrackModel::dbOverridePath()
{
    return QCoreApplication::applicationDirPath() + "/track-db.json";
}

TrackModel::TrackModel(RaceBoxModel *raceBoxModel, bool mockMode, QObject *parent)
    : QObject(parent)
    , m_raceBoxModel(raceBoxModel)
    , m_mockMode(mockMode)
{
    m_detectTimer.setInterval(kDetectIntervalMs);
    connect(&m_detectTimer, &QTimer::timeout, this, &TrackModel::scanNearestTrack);

    connect(m_raceBoxModel, &RaceBoxModel::hasFixChanged, this, [this]() {
        if (m_raceBoxModel->hasFix() && !m_manuallySelectedThisRun && m_suggestedTrackId.isEmpty())
            m_detectTimer.start();
    });

    loadDatabase();
    loadUserState();
    rebuildFiltered();
}

void TrackModel::loadDatabase()
{
    QFile overrideFile(dbOverridePath());
    bool loaded = false;
    if (overrideFile.exists() && overrideFile.open(QIODevice::ReadOnly)) {
        loaded = loadTracksFromJson(overrideFile.readAll());
        if (loaded)
            qCInfo(lcApp) << "Loaded track DB from downloaded copy —" << m_tracks.size() << "tracks";
    }

    if (!loaded) {
        QFile qrcFile(":/data/track-db.json");
        if (qrcFile.open(QIODevice::ReadOnly)) {
            loaded = loadTracksFromJson(qrcFile.readAll());
            if (loaded)
                qCInfo(lcApp) << "Loaded bundled track DB —" << m_tracks.size() << "tracks";
        }
    }

    if (!loaded)
        qCWarning(lcApp) << "Could not load track database from any source";

    injectMockTrackIfNeeded();
    rebuildCountries();
}

bool TrackModel::loadTracksFromJson(const QByteArray &data)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty())
        return false;

    QVector<Track> tracks;
    tracks.reserve(doc.array().size());
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject obj = v.toObject();
        const QJsonArray c = obj["c"].toArray();
        if (!obj.contains("id") || !obj.contains("name") || c.size() < 2)
            continue;

        Track t;
        t.id      = obj["id"].toString();
        t.name    = obj["name"].toString();
        t.country = obj["country"].toString();
        t.lat     = c[0].toDouble();
        t.lon     = c[1].toDouble();
        for (const QJsonValue &cfg : obj["configurations"].toArray())
            t.configs.append(cfg.toString());
        tracks.append(t);
    }

    if (tracks.isEmpty())
        return false;

    m_tracks = tracks;
    return true;
}

void TrackModel::injectMockTrackIfNeeded()
{
    if (!m_mockMode)
        return;
    for (const Track &t : m_tracks) {
        if (t.id == QLatin1String(kMockTrackId))
            return;
    }
    Track mock;
    mock.id      = kMockTrackId;
    mock.name    = "MOCK CIRCUIT (LONDON)";
    mock.country = "Mock";
    mock.lat     = 51.5074;
    mock.lon     = -0.1278;
    m_tracks.append(mock);
}

void TrackModel::rebuildCountries()
{
    QSet<QString> set;
    for (const Track &t : m_tracks)
        set.insert(t.country);
    QStringList list(set.begin(), set.end());
    std::sort(list.begin(), list.end());
    list.prepend("ALL");
    m_countries = list;
}

const TrackModel::Track *TrackModel::findTrack(const QString &id) const
{
    for (const Track &t : m_tracks) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

QString TrackModel::activeTrackName() const
{
    const Track *t = findTrack(m_activeTrackId);
    return t ? t->name : QString();
}

QString TrackModel::suggestedTrackName() const
{
    const Track *t = findTrack(m_suggestedTrackId);
    return t ? t->name : QString();
}

void TrackModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    emit filtersChanged();
    rebuildFiltered();
}

void TrackModel::setCountryFilter(const QString &country)
{
    if (m_countryFilter == country)
        return;
    m_countryFilter = country;
    emit filtersChanged();
    rebuildFiltered();
}

void TrackModel::rebuildFiltered()
{
    QVector<Track> matched;
    const QString needle = m_searchText.trimmed().toLower();
    for (const Track &t : m_tracks) {
        if (m_countryFilter != "ALL" && t.country != m_countryFilter)
            continue;
        if (!needle.isEmpty() && !t.name.toLower().contains(needle))
            continue;
        matched.append(t);
    }

    QVector<Track> favorites, others;
    for (const Track &t : matched) {
        if (m_favorites.contains(t.id))
            favorites.append(t);
        else
            others.append(t);
    }
    auto byName = [](const Track &a, const Track &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; };
    std::sort(favorites.begin(), favorites.end(), byName);
    std::sort(others.begin(), others.end(), byName);

    QVariantList result;
    result.reserve(favorites.size() + others.size());
    for (const QVector<Track> *group : {&favorites, &others}) {
        for (const Track &t : *group) {
            QVariantMap row;
            row["id"]            = t.id;
            row["name"]          = t.name;
            row["country"]       = t.country;
            row["isFavorite"]    = m_favorites.contains(t.id);
            row["hasFinishLine"] = m_finishLines.contains(t.id);
            row["isActive"]      = (t.id == m_activeTrackId);
            result.append(row);
        }
    }
    m_filteredTracks = result;
    emit filteredTracksChanged();
}

void TrackModel::loadUserState()
{
    QFile f(userStatePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcApp) << "tracks-user.json parse error:" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    m_activeTrackId = obj["activeTrackId"].toString();

    for (const QJsonValue &v : obj["favorites"].toArray())
        m_favorites.insert(v.toString());

    const QJsonObject finishLines = obj["finishLines"].toObject();
    for (auto it = finishLines.begin(); it != finishLines.end(); ++it) {
        const QJsonObject fl = it.value().toObject();
        QVariantMap map;
        map["lat"]     = fl["lat"].toDouble();
        map["lon"]     = fl["lon"].toDouble();
        map["radiusM"] = fl["radiusM"].toDouble(kDefaultFinishRadiusM);
        m_finishLines[it.key()] = map;
    }

    qCInfo(lcApp) << "Loaded track user state — active:" << m_activeTrackId
                  << "| favorites:" << m_favorites.size()
                  << "| stored finish lines:" << m_finishLines.size();
}

void TrackModel::persistUserState()
{
    QJsonObject obj;
    obj["activeTrackId"] = m_activeTrackId;

    QJsonArray favArr;
    for (const QString &id : m_favorites)
        favArr.append(id);
    obj["favorites"] = favArr;

    QJsonObject flObj;
    for (auto it = m_finishLines.constBegin(); it != m_finishLines.constEnd(); ++it) {
        QJsonObject fl;
        fl["lat"]     = it.value()["lat"].toDouble();
        fl["lon"]     = it.value()["lon"].toDouble();
        fl["radiusM"] = it.value()["radiusM"].toDouble();
        flObj[it.key()] = fl;
    }
    obj["finishLines"] = flObj;

    QFile f(userStatePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcApp) << "Could not write tracks-user.json";
        return;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

void TrackModel::setActiveTrack(const QString &id, bool autoDetected)
{
    m_activeTrackId = id;
    m_autoDetected  = autoDetected;
    persistUserState();
    emit activeTrackChanged();
    rebuildFiltered();
}

void TrackModel::selectTrack(const QString &id)
{
    if (!findTrack(id))
        return;

    m_manuallySelectedThisRun = true;
    m_detectTimer.stop();
    if (!m_suggestedTrackId.isEmpty()) {
        m_suggestedTrackId.clear();
        emit trackSuggestionChanged();
    }

    setActiveTrack(id, false);

    if (m_finishLines.contains(id)) {
        const QVariantMap fl = m_finishLines.value(id);
        emit applyFinishLine(fl["lat"].toDouble(), fl["lon"].toDouble(), fl["radiusM"].toDouble());
    } else {
        emit clearFinishLineRequested();
    }
}

void TrackModel::clearActiveTrack()
{
    m_activeTrackId.clear();
    m_autoDetected = false;
    m_manuallySelectedThisRun = false;
    persistUserState();
    emit activeTrackChanged();
    rebuildFiltered();

    if (m_raceBoxModel->hasFix())
        m_detectTimer.start();
}

void TrackModel::toggleFavorite(const QString &id)
{
    if (m_favorites.contains(id))
        m_favorites.remove(id);
    else
        m_favorites.insert(id);
    persistUserState();
    rebuildFiltered();
}

void TrackModel::scanNearestTrack()
{
    if (m_manuallySelectedThisRun) {
        m_detectTimer.stop();
        return;
    }

    const double lat = m_raceBoxModel->lastLat();
    const double lon = m_raceBoxModel->lastLon();

    const Track *nearest = nullptr;
    double nearestDist = kDetectRadiusM;

    for (const Track &t : m_tracks) {
        if (t.id == m_activeTrackId || m_dismissedThisRun.contains(t.id))
            continue;
        // Cheap rectangular prefilter before haversine
        if (std::abs(t.lat - lat) > 0.05 || std::abs(t.lon - lon) > 0.05)
            continue;
        const double dist = RaceBoxModel::haversineM(lat, lon, t.lat, t.lon);
        if (dist <= nearestDist) {
            nearest = &t;
            nearestDist = dist;
        }
    }

    if (nearest) {
        m_suggestedTrackId = nearest->id;
        emit trackSuggestionChanged();
        m_detectTimer.stop();
        qCInfo(lcApp) << "Track suggested —" << nearest->name << "(" << nearestDist << "m )";
    }
}

void TrackModel::acceptSuggestedTrack()
{
    if (m_suggestedTrackId.isEmpty())
        return;

    const QString id = m_suggestedTrackId;
    m_suggestedTrackId.clear();
    emit trackSuggestionChanged();

    setActiveTrack(id, true);

    if (m_finishLines.contains(id)) {
        const QVariantMap fl = m_finishLines.value(id);
        emit applyFinishLine(fl["lat"].toDouble(), fl["lon"].toDouble(), fl["radiusM"].toDouble());
    }
    // else: leave the current finish line untouched (global/mock fallback stays active)
}

void TrackModel::dismissSuggestedTrack()
{
    if (m_suggestedTrackId.isEmpty())
        return;

    m_dismissedThisRun.insert(m_suggestedTrackId);
    m_suggestedTrackId.clear();
    emit trackSuggestionChanged();

    if (m_raceBoxModel->hasFix() && !m_manuallySelectedThisRun)
        m_detectTimer.start();
}

void TrackModel::onFinishLineLearned(double lat, double lon)
{
    if (m_activeTrackId.isEmpty())
        return; // no active track — global DashConfig fallback handles this independently

    if (lat == 0.0 && lon == 0.0) {
        if (m_finishLines.remove(m_activeTrackId) > 0) {
            persistUserState();
            emit activeTrackChanged();
            rebuildFiltered();
        }
        return;
    }

    QVariantMap fl;
    fl["lat"]     = lat;
    fl["lon"]     = lon;
    fl["radiusM"] = kDefaultFinishRadiusM;
    m_finishLines[m_activeTrackId] = fl;
    persistUserState();
    emit activeTrackChanged();
    rebuildFiltered();
}

void TrackModel::applyStartupFinishLine()
{
    if (m_activeTrackId.isEmpty() || !m_finishLines.contains(m_activeTrackId))
        return;
    const QVariantMap fl = m_finishLines.value(m_activeTrackId);
    emit applyFinishLine(fl["lat"].toDouble(), fl["lon"].toDouble(), fl["radiusM"].toDouble());
    qCInfo(lcApp) << "Applied stored per-track finish line for" << activeTrackName();
}

void TrackModel::refreshDatabase()
{
    if (m_refreshing)
        return;

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    m_refreshing = true;
    m_refreshError.clear();
    emit refreshStateChanged();

    QNetworkRequest req(QUrl("https://www.racebox.pro/info/tracks/xhr"));
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_refreshing = false;

        if (reply->error() != QNetworkReply::NoError) {
            m_refreshError = reply->errorString();
            emit refreshStateChanged();
            qCWarning(lcApp) << "Track DB refresh failed:" << m_refreshError;
            return;
        }

        const QByteArray data = reply->readAll();
        if (!loadTracksFromJson(data)) {
            m_refreshError = "Invalid response from server";
            emit refreshStateChanged();
            qCWarning(lcApp) << "Track DB refresh: invalid payload";
            return;
        }

        QFile f(dbOverridePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(data);

        injectMockTrackIfNeeded();
        rebuildCountries();
        rebuildFiltered();
        emit refreshStateChanged();
        qCInfo(lcApp) << "Track DB refreshed —" << m_tracks.size() << "tracks";
    });
}

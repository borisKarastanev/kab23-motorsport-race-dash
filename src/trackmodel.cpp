#include "trackmodel.h"
#include "raceboxmodel.h"
#include "logging.h"
#include "apppaths.h"

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
// Above this, the car is being driven rather than parked — matches the GPS-jitter
// tolerance RaceBoxModel uses to distinguish "stopped" from "genuinely moving".
constexpr int    kStationaryMaxKmh = 3;
const char *kMockTrackId = "mock-track";
}

QString TrackModel::userStatePath()
{
    return AppPaths::dataFile("tracks-user.json");
}

QString TrackModel::dbOverridePath()
{
    return AppPaths::dataFile("track-db.json");
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
    connect(m_raceBoxModel, &RaceBoxModel::speedKmhChanged, this, [this](int kmh) {
        // A suggestion that surfaced while parked must not linger on screen once
        // the car pulls away — the timer is stopped while a suggestion is pending,
        // so withdrawing it has to be driven by the speed change, not the scan.
        // Resume scanning so it can re-appear at the next stop. New suggestions are
        // separately suppressed in scanNearestTrack.
        if (kmh > kStationaryMaxKmh && !m_suggestedTrackId.isEmpty()) {
            m_suggestedTrackId.clear();
            emit trackSuggestionChanged();
            if (m_raceBoxModel->hasFix() && !m_manuallySelectedThisRun && m_activeTrackId.isEmpty())
                m_detectTimer.start();
        }
    });

    loadUserState(); // cheap; needed by applyStartupFinishLine() before the DB is ready

    // The bundled track DB (~216 KB / ~1500 entries) costs tens of ms to parse and
    // turn into filtered rows, and only feeds the Tracks browser and the GPS-gated,
    // self-retrying auto-detect scan — never the first frame. Defer it to the first
    // event-loop pass so it doesn't stall cold start.
    QTimer::singleShot(0, this, [this]() {
        loadDatabase();
        rebuildFiltered();
        emit activeTrackChanged(); // active-track name/flags are resolvable now
    });
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
        t.id        = obj["id"].toString();
        t.name      = obj["name"].toString();
        t.nameLower = t.name.toLower();
        t.country   = obj["country"].toString();
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
    mock.id        = kMockTrackId;
    mock.name      = "MOCK CIRCUIT (LONDON)";
    mock.nameLower = mock.name.toLower();
    mock.country   = "Mock";
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
    emit countriesChanged();
}

const TrackModel::Track *TrackModel::findTrack(const QString &id) const
{
    for (const Track &t : m_tracks) {
        if (t.id == id)
            return &t;
    }
    return nullptr;
}

bool TrackModel::emitFinishLineFor(const QString &id)
{
    if (!m_finishLines.contains(id))
        return false;
    const QVariantMap fl = m_finishLines.value(id);
    emit applyFinishLine(fl["lat1"].toDouble(), fl["lon1"].toDouble(),
                         fl["lat2"].toDouble(), fl["lon2"].toDouble());
    return true;
}

QVariantMap TrackModel::finishLineFor(const QString &trackId) const
{
    if (m_finishLines.contains(trackId))
        return m_finishLines.value(trackId);
    return m_finishLines.value(QString()); // global fallback ({} if none)
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
        if (!needle.isEmpty() && !t.nameLower.contains(needle))
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
        // Value is a flat [lat1, lon1, lat2, lon2] gate (RaceBox "startLine" format).
        const QJsonValue val = it.value();
        if (val.isObject()) {
            // Legacy point+radius circle. A circle carries no crossing direction, so
            // it can't be converted to an oriented gate — warn instead of silently
            // dropping it, so the user knows to re-learn this finish line.
            qCWarning(lcApp) << "Ignoring legacy circle finish line for"
                             << (it.key().isEmpty() ? QStringLiteral("(global)") : it.key())
                             << "— re-learn it to store a gate";
            continue;
        }
        const QJsonArray a = val.toArray();
        if (a.size() < 4) {
            qCWarning(lcApp) << "Ignoring malformed finish line for"
                             << (it.key().isEmpty() ? QStringLiteral("(global)") : it.key());
            continue;
        }
        QVariantMap map;
        map["lat1"] = a[0].toDouble(); map["lon1"] = a[1].toDouble();
        map["lat2"] = a[2].toDouble(); map["lon2"] = a[3].toDouble();
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
        const QVariantMap &m = it.value();
        const QJsonArray a { m["lat1"].toDouble(), m["lon1"].toDouble(),
                             m["lat2"].toDouble(), m["lon2"].toDouble() };
        flObj[it.key()] = a;
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

    // Prefer the track's own finish line, else the global one, else clear.
    if (!emitFinishLineFor(id) && !emitFinishLineFor(QString()))
        emit clearFinishLineRequested();
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
    if (m_manuallySelectedThisRun || !m_activeTrackId.isEmpty()) {
        m_detectTimer.stop();
        return;
    }

    // Only prompt while parked — a suggestion popping up mid-drive is a
    // distraction, and the driver can't safely deal with it anyway. Keep the
    // timer running so the scan retries once the car stops.
    if (m_raceBoxModel->speedKmh() > kStationaryMaxKmh)
        return;

    const double lat = m_raceBoxModel->lastLat();
    const double lon = m_raceBoxModel->lastLon();

    const Track *nearest = nullptr;
    double nearestDist = kDetectRadiusM;

    for (const Track &t : m_tracks) {
        if (m_dismissedThisRun.contains(t.id))
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

    // Prefer the track's own finish line, else the global one. If neither exists,
    // leave the current finish line untouched (mock fallback stays active).
    if (!emitFinishLineFor(id))
        emitFinishLineFor(QString());
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

void TrackModel::onFinishLineLearned(double latA, double lonA, double latB, double lonB)
{
    // Keyed by active track id; the empty id ("no track active") holds the
    // global finish line. This is the single persistence path for both cases.
    if (latA == 0.0 && lonA == 0.0 && latB == 0.0 && lonB == 0.0) {
        if (m_finishLines.remove(m_activeTrackId) > 0) {
            persistUserState();
            emit activeTrackChanged();
            rebuildFiltered();
        }
        return;
    }

    QVariantMap fl;
    fl["lat1"] = latA; fl["lon1"] = lonA;
    fl["lat2"] = latB; fl["lon2"] = lonB;
    m_finishLines[m_activeTrackId] = fl;
    persistUserState();
    emit activeTrackChanged();
    rebuildFiltered();
}

void TrackModel::applyStartupFinishLine()
{
    // Active track's own line takes precedence; fall back to the global one.
    if (emitFinishLineFor(m_activeTrackId)) {
        qCInfo(lcApp) << "Applied stored finish line for"
                      << (m_activeTrackId.isEmpty() ? QStringLiteral("(no track)") : activeTrackName());
        return;
    }
    if (!m_activeTrackId.isEmpty() && emitFinishLineFor(QString()))
        qCInfo(lcApp) << "Applied global finish line (no per-track line for" << activeTrackName() << ")";
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

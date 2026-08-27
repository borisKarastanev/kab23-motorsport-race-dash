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
const char *kMockTrackId = "mock-track";

// The on-disk gate shape, in one place. A gate persists as a flat
// [lat1, lon1, lat2, lon2] array (the RaceBox "startLine" format, so a line can
// be typed in by hand), optionally followed by the latched crossing direction —
// finish lines store the four coordinates, sector gates append the fifth field.
// Both finishLines and sectorGates entries are read and written through this
// pair, so the format has a single definition rather than one per call site.
constexpr int kGateArrayCoords = 4;

QVariantMap gateMapFromArray(const QJsonArray &a, bool withDir)
{
    QVariantMap map;
    map["lat1"] = a[0].toDouble(); map["lon1"] = a[1].toDouble();
    map["lat2"] = a[2].toDouble(); map["lon2"] = a[3].toDouble();
    // Sector gates only. Absent in files written before direction was recorded,
    // where 0 correctly means "re-latch on first crossing". Finish lines never
    // carry one — theirs is latched live in RaceBoxModel, not persisted — so the
    // key stays off those maps rather than sitting there as a misleading 0.
    if (withDir)
        map["dir"] = a.size() > kGateArrayCoords ? a[kGateArrayCoords].toInt() : 0;
    return map;
}

QJsonArray gateArrayFromMap(const QVariantMap &m, bool withDir)
{
    QJsonArray a { m["lat1"].toDouble(), m["lon1"].toDouble(),
                   m["lat2"].toDouble(), m["lon2"].toDouble() };
    if (withDir)
        a.append(m["dir"].toInt());
    return a;
}
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
        if (kmh > kStationarySpeedKmh && !m_suggestedTrackId.isEmpty()) {
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
        // Confirmed S/F lines live in the track DB, which only finishes loading
        // here — after main.cpp's early applyStartupFinishLine() ran against an
        // empty m_tracks and therefore couldn't see them. Re-apply now so a
        // persisted active track that carries a confirmed line gets its gate
        // pushed to RaceBoxModel. Idempotent: setFinishLine just sets state.
        applyStartupFinishLine();
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

        // Optional confirmed S/F gate baked into the DB entry itself, as a flat
        // [lat1, lon1, lat2, lon2] array (same shape as the user-learned finish
        // lines in tracks-user.json). Present only for tracks we've pinned down
        // from real session data — see data/track-db.json. Locked: the UI hides
        // the learn/reset control whenever a track carries one of these.
        const QJsonArray start = obj["start"].toArray();
        if (start.size() >= 4) {
            QVariantMap fl;
            fl["lat1"] = start[0].toDouble(); fl["lon1"] = start[1].toDouble();
            fl["lat2"] = start[2].toDouble(); fl["lon2"] = start[3].toDouble();
            t.confirmedFinishLine = fl;
        }
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

bool TrackModel::isFinishLineLocked(const QString &id) const
{
    const Track *t = findTrack(id);
    return t && !t->confirmedFinishLine.isEmpty();
}

QVariantMap TrackModel::gateFor(const QString &id) const
{
    // Precedence: a track's confirmed DB gate is authoritative and cannot be
    // overridden — onFinishLineLearned() refuses to store a learned line against
    // a locked track, and the UI hides the control entirely. A learned entry can
    // still exist in tracks-user.json from before that lock was enforced (or
    // from a hand-edited file); the DB gate wins over it rather than being
    // masked by it, because a locked track offers no way to reset a stale
    // learned line. Only tracks with no confirmed gate fall through to the
    // user-learned one. No global ("") fallback here — callers add it where
    // appropriate.
    const Track *t = findTrack(id);
    if (t && !t->confirmedFinishLine.isEmpty())
        return t->confirmedFinishLine;
    return m_finishLines.value(id); // {} when the track has no learned line either
}

bool TrackModel::emitFinishLineFor(const QString &id)
{
    const QVariantMap fl = gateFor(id);
    if (fl.isEmpty())
        return false;
    emit applyFinishLine(fl["lat1"].toDouble(), fl["lon1"].toDouble(),
                         fl["lat2"].toDouble(), fl["lon2"].toDouble());
    // Always paired with the finish line, including an empty list when id has
    // none stored — otherwise a track with no gates yet would inherit
    // whichever gates happened to already be live from the previously active
    // track. Read from and (see onSectorGatesLearned) written back to the same
    // slot the line itself resolved from.
    m_gateSlotId = id;
    emit applySectorGates(m_sectorGates.value(id));
    return true;
}

QVariantMap TrackModel::finishLineFor(const QString &trackId) const
{
    const QVariantMap fl = gateFor(trackId);
    if (!fl.isEmpty())
        return fl;
    return m_finishLines.value(QString()); // global fallback ({} if none)
}

bool TrackModel::activeTrackHasFinishLine() const
{
    return !gateFor(m_activeTrackId).isEmpty();
}

bool TrackModel::activeTrackFinishLineLocked() const
{
    return isFinishLineLocked(m_activeTrackId);
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
            // t is in hand — read its confirmed gate directly rather than
            // isFinishLineLocked(t.id), which would re-scan all tracks per row (O(n^2)).
            row["hasFinishLine"] = !t.confirmedFinishLine.isEmpty() || m_finishLines.contains(t.id);
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
    m_gateSlotId    = m_activeTrackId; // until applyStartupFinishLine() resolves the real slot

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
        if (a.size() < kGateArrayCoords) {
            qCWarning(lcApp) << "Ignoring malformed finish line for"
                             << (it.key().isEmpty() ? QStringLiteral("(global)") : it.key());
            continue;
        }
        m_finishLines[it.key()] = gateMapFromArray(a, /*withDir=*/false);
    }

    // sectorGates: trackId -> array of gates in the shape gateMapFromArray()
    // defines, each carrying its latched direction. The whole key is absent in
    // files saved before sector gates existed — an empty object here is exactly
    // that case.
    const QJsonObject sectorGates = obj["sectorGates"].toObject();
    for (auto it = sectorGates.begin(); it != sectorGates.end(); ++it) {
        QVariantList gates;
        bool malformed = false;
        for (const QJsonValue &gv : it.value().toArray()) {
            const QJsonArray a = gv.toArray();
            if (a.size() < kGateArrayCoords) { malformed = true; break; }
            gates.append(gateMapFromArray(a, /*withDir=*/true));
        }
        // All or nothing: a gate list is only meaningful at the length it was
        // derived at. Keeping the survivors of a partly-corrupt entry yields a
        // short list, which silently changes the track's sector count — every
        // lap then fails the optimal lap's eligibility test, and derivation
        // never re-runs (it is skipped whenever any gates exist), so the loss is
        // permanent and persistUserState() writes the truncation back out.
        if (malformed) {
            qCWarning(lcApp) << "Discarding malformed sector gates for"
                             << (it.key().isEmpty() ? QStringLiteral("(global)") : it.key())
                             << "— they will be re-derived from the next completed lap";
            continue;
        }
        if (!gates.isEmpty())
            m_sectorGates[it.key()] = gates;
    }

    qCInfo(lcApp) << "Loaded track user state — active:" << m_activeTrackId
                  << "| favorites:" << m_favorites.size()
                  << "| stored finish lines:" << m_finishLines.size()
                  << "| stored sector gates:" << m_sectorGates.size();
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
    for (auto it = m_finishLines.constBegin(); it != m_finishLines.constEnd(); ++it)
        flObj[it.key()] = gateArrayFromMap(it.value(), /*withDir=*/false);
    obj["finishLines"] = flObj;

    QJsonObject sgObj;
    for (auto it = m_sectorGates.constBegin(); it != m_sectorGates.constEnd(); ++it) {
        QJsonArray gatesArr;
        for (const QVariant &gv : it.value())
            gatesArr.append(gateArrayFromMap(gv.toMap(), /*withDir=*/true));
        sgObj[it.key()] = gatesArr;
    }
    obj["sectorGates"] = sgObj;

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
    // Default the gate slot to the incoming track; the emitFinishLineFor() the
    // callers run next overrides it with the slot the line actually resolved
    // from (possibly the global one). Without this, a track that ends up with
    // no line at all would leave the *previous* track's slot latched, and the
    // resulting clearFinishLineRequested() round-trip would delete that
    // track's saved gates.
    m_gateSlotId    = id;
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
    if (m_raceBoxModel->speedKmh() > kStationarySpeedKmh)
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
    // A track carrying a confirmed "start" gate in the DB has a surveyed S/F
    // line, and that line is not the driver's to move — neither by learning a
    // new one nor by resetting it. Tracks without one (a circuit the DB doesn't
    // pin down, the mock track, or the global "no track active" slot used on
    // the road) stay fully learnable. Guards both branches below: there is no
    // user-learned entry for a locked track to remove either.
    if (isFinishLineLocked(m_activeTrackId))
        return;

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
    // The active track now owns a line of its own, so it owns the gate slot too
    // — even if it had been borrowing the global line until this moment.
    m_gateSlotId = m_activeTrackId;
    persistUserState();
    emit activeTrackChanged();
    rebuildFiltered();
}

void TrackModel::onSectorGatesLearned(const QVariantList &gates)
{
    // Keyed by m_gateSlotId — the slot the live finish line resolved from —
    // rather than by m_activeTrackId, which the two differ from whenever a
    // track is running on the global ("") line. Gates are geometry derived
    // against that line, so they belong wherever the line does; storing them
    // under the active track instead puts them where emitFinishLineFor() will
    // never read them back, and lap 1 of every future session re-derives them.
    if (gates.isEmpty()) {
        if (m_sectorGates.remove(m_gateSlotId) > 0)
            persistUserState();
        return;
    }
    m_sectorGates[m_gateSlotId] = gates;
    persistUserState();
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

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QJsonDocument>

class RaceBoxModel;
class QNetworkAccessManager;

class TrackModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList countries READ countries NOTIFY countriesChanged)
    Q_PROPERTY(QVariantList filteredTracks READ filteredTracks NOTIFY filteredTracksChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filtersChanged)
    Q_PROPERTY(QString countryFilter READ countryFilter WRITE setCountryFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString activeTrackId READ activeTrackId NOTIFY activeTrackChanged)
    Q_PROPERTY(QString activeTrackName READ activeTrackName NOTIFY activeTrackChanged)
    Q_PROPERTY(bool activeTrackHasFinishLine READ activeTrackHasFinishLine NOTIFY activeTrackChanged)
    Q_PROPERTY(bool autoDetected READ autoDetected NOTIFY activeTrackChanged)
    Q_PROPERTY(QString suggestedTrackId READ suggestedTrackId NOTIFY trackSuggestionChanged)
    Q_PROPERTY(QString suggestedTrackName READ suggestedTrackName NOTIFY trackSuggestionChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshStateChanged)
    Q_PROPERTY(QString refreshError READ refreshError NOTIFY refreshStateChanged)

public:
    explicit TrackModel(RaceBoxModel *raceBoxModel, bool mockMode, QObject *parent = nullptr);

    QStringList countries() const { return m_countries; }
    QVariantList filteredTracks() const { return m_filteredTracks; }
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QString countryFilter() const { return m_countryFilter; }
    void setCountryFilter(const QString &country);
    QString activeTrackId() const { return m_activeTrackId; }
    QString activeTrackName() const;
    bool activeTrackHasFinishLine() const { return m_finishLines.contains(m_activeTrackId); }
    bool autoDetected() const { return m_autoDetected; }
    QString suggestedTrackId() const { return m_suggestedTrackId; }
    QString suggestedTrackName() const;
    bool refreshing() const { return m_refreshing; }
    QString refreshError() const { return m_refreshError; }

    Q_INVOKABLE void selectTrack(const QString &id);
    Q_INVOKABLE void clearActiveTrack();
    Q_INVOKABLE void toggleFavorite(const QString &id);
    Q_INVOKABLE void acceptSuggestedTrack();
    Q_INVOKABLE void dismissSuggestedTrack();
    Q_INVOKABLE void refreshDatabase();

    // Effective finish line for a track id: the track's own stored gate, or the
    // global (no-track) gate as a fallback. Empty map if neither exists.
    // Keys: "lat1", "lon1", "lat2", "lon2" (gate endpoints). Used by the session map.
    Q_INVOKABLE QVariantMap finishLineFor(const QString &trackId) const;

    // Called once from main.cpp after signal wiring: applies the persisted
    // finish line for the active track, or the global one if no track is
    // active / the active track has none.
    void applyStartupFinishLine();

public slots:
    // Connected in main.cpp to RaceBoxModel::finishLineLearned. Coordinates are
    // the two endpoints of the finish-line gate (all zero clears it).
    void onFinishLineLearned(double latA, double lonA, double latB, double lonB);

signals:
    void countriesChanged();
    void filteredTracksChanged();
    void filtersChanged();
    void activeTrackChanged();
    void trackSuggestionChanged();
    void refreshStateChanged();
    // Connected in main.cpp to RaceBoxModel::setFinishLine — the gate endpoints A→B
    void applyFinishLine(double latA, double lonA, double latB, double lonB);
    // Connected in main.cpp to RaceBoxModel::clearFinishLine
    void clearFinishLineRequested();

private slots:
    void scanNearestTrack();

private:
    struct Track {
        QString id, name, nameLower, country;
        double lat = 0.0, lon = 0.0;
        QStringList configs;
    };

    void loadDatabase();
    bool loadTracksFromJson(const QByteArray &data);
    void injectMockTrackIfNeeded();
    void rebuildCountries();
    void loadUserState();
    void persistUserState();
    void rebuildFiltered();
    void setActiveTrack(const QString &id, bool autoDetected);
    const Track *findTrack(const QString &id) const;
    // Emits applyFinishLine for the stored finish line of id, if any.
    // Returns true if a finish line was emitted.
    bool emitFinishLineFor(const QString &id);
    static QString userStatePath();
    static QString dbOverridePath();

    RaceBoxModel *m_raceBoxModel;
    bool m_mockMode;

    QVector<Track> m_tracks;
    QStringList m_countries;

    QSet<QString> m_favorites;
    // Track id -> {lat1, lon1, lat2, lon2} gate endpoints. The empty-string key
    // holds the global finish line used when no track is active. Single owner of
    // all finish lines. Persisted as a flat [lat1,lon1,lat2,lon2] "startLine"
    // array (matching the RaceBox export format for easy manual entry).
    QHash<QString, QVariantMap> m_finishLines;
    QSet<QString> m_dismissedThisRun;

    QString m_searchText;
    QString m_countryFilter = "ALL";
    QVariantList m_filteredTracks;

    QString m_activeTrackId;
    bool m_manuallySelectedThisRun = false;
    bool m_autoDetected = false;

    QString m_suggestedTrackId;

    QTimer m_detectTimer;

    bool m_refreshing = false;
    QString m_refreshError;
    QNetworkAccessManager *m_nam = nullptr;
};

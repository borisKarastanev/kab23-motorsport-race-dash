#pragma once

#include <QObject>
#include <QVariantList>
#include <QList>

class CanDataModel;
class RaceBoxModel;
class TrackModel;

class SessionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QVariantList sessionGroups READ sessionGroups NOTIFY sessionsChanged)
    Q_PROPERTY(bool hasLaps READ hasLaps NOTIFY hasLapsChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged)

public:
    explicit SessionModel(CanDataModel *canModel, RaceBoxModel *raceBoxModel,
                          TrackModel *trackModel, QObject *parent = nullptr);

    const QVariantList &sessions() const { return m_sessions; }
    // Sessions bucketed by track (untagged sessions fall into "Unknown"),
    // newest-first. Cached; rebuilt only when m_sessions changes.
    const QVariantList &sessionGroups() const { return m_sessionGroups; }
    bool hasLaps() const { return !m_currentLapTimes.isEmpty(); }
    // A session may be saved only when laps have been recorded and the car is
    // stationary (or crawling) — owns the has-laps + speed-threshold policy so
    // the view just binds a single flag.
    bool canSave() const { return m_canSave; }

    Q_INVOKABLE void saveCurrentSession();
    // No-op (with a warning logged) if no session matches timestampIso.
    Q_INVOKABLE void deleteSession(const QString &timestampIso);
    // Whether any saved session still belongs to trackId (empty = the "unknown"
    // bucket). Drives post-delete navigation: the Details view pops one step
    // back to the track's remaining sessions when true, or all the way to the
    // track-groups list when the last session for the track is gone.
    Q_INVOKABLE bool hasSessionsForTrack(const QString &trackId) const;

signals:
    void sessionsChanged();
    void hasLapsChanged();
    void canSaveChanged();
    // Emitted after a session is persisted — RaceBoxModel listens to reset its
    // own lap counters, keeping the two models decoupled (signal-only, no
    // direct method calls in either direction).
    void sessionSaved();

private slots:
    void onLapCompleted(qint64 ms, const QVariantList &path);
    void onLapSectorsCompleted(const QList<qint64> &sectorMs);
    void onSpeedChanged();
    void onOilTempChanged();
    void onCoolantTempChanged();

private:
    void load();
    void persist();
    void rebuildSessionGroups();
    void updateCanSave();
    static QString sessionsPath();

    CanDataModel  *m_canModel     = nullptr;
    RaceBoxModel  *m_raceBoxModel = nullptr;
    TrackModel    *m_trackModel   = nullptr;

    QVariantList   m_sessions;
    QVariantList   m_sessionGroups; // cached grouping of m_sessions, rebuilt on change

    QList<qint64>       m_currentLapTimes;
    QList<QVariantList> m_currentLapPaths;
    // Parallel to m_currentLapTimes: lap i's sector splits, or an empty list if
    // lap i missed a sector gate. Lap *number* for OptimalLap::SectoredLap is
    // this list's 1-based index — it resets to empty in lockstep with
    // m_currentLapTimes (both driven by the same pair of RaceBoxModel signals
    // for the same completed lap), so the two indices always agree with
    // RaceBoxModel's own m_lapNumber for the laps recorded so far this session.
    // Read only at save time (see saveCurrentSession()) — the optimal lap is a
    // property of a *completed* session, shown in the Sessions view, not a
    // live dashboard readout.
    QList<QList<qint64>> m_currentLapSectorTimes;
    int            m_topSpeedKmh = 0;
    double         m_maxOilC     = 0.0;
    double         m_maxCoolantC = 0.0;
    bool           m_canSave     = false;
};

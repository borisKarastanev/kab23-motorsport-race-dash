#pragma once

#include <QObject>
#include <QVariantList>
#include <QList>

#include "raceboxmodel.h"

class CanDataModel;
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
    bool hasLaps() const { return !m_currentLaps.isEmpty(); }
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
    void onLapCompleted(const RaceBoxLapResult &lap);
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

    // One record per completed lap this session, in arrival order.
    //
    // lapNumber is assigned *here*, 1-based over this list, deliberately rather
    // than taken from RaceBoxLapResult::lapNumber. The two agree on every path
    // but one: clearFinishLine() resets RaceBoxModel's counter to 0 mid-session
    // (resetLapState()) without clearing this list, so a driver who re-learns
    // the line mid-run would otherwise bank laps numbered 1,2,3,1,2,3,4. That
    // ambiguity reaches the saved record — OptimalLap::compute() treats
    // lapNumber as an identity, both in its tie-break and in matchesLapNumber —
    // so the number a session files a lap under has to be the session's own,
    // monotonic and unique within it, not the lap timer's.
    //
    // Read only at save time (see saveCurrentSession()) — the optimal lap is a
    // property of a *completed* session, shown in the Sessions view, not a
    // live dashboard readout.
    struct LapRecord {
        int           lapNumber = 0;
        qint64        ms        = 0;
        QVariantList  path;
        QList<qint64> sectorMs;
    };
    QList<LapRecord> m_currentLaps;
    int            m_topSpeedKmh = 0;
    double         m_maxOilC     = 0.0;
    double         m_maxCoolantC = 0.0;
    bool           m_canSave     = false;
};

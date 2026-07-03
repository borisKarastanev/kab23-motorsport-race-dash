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

public:
    explicit SessionModel(CanDataModel *canModel, RaceBoxModel *raceBoxModel,
                          TrackModel *trackModel, QObject *parent = nullptr);

    const QVariantList &sessions() const { return m_sessions; }
    // Sessions bucketed by track (untagged sessions fall into "Unknown"),
    // newest-first — derived on demand from m_sessions, not stored.
    QVariantList sessionGroups() const;
    bool hasLaps() const { return !m_currentLapTimes.isEmpty(); }

    Q_INVOKABLE void saveCurrentSession();

signals:
    void sessionsChanged();
    void hasLapsChanged();
    // Emitted after a session is persisted — RaceBoxModel listens to reset its
    // own lap counters, keeping the two models decoupled (signal-only, no
    // direct method calls in either direction).
    void sessionSaved();

private slots:
    void onLapCompleted(qint64 ms, const QVariantList &path);
    void onSpeedChanged();
    void onOilTempChanged();
    void onCoolantTempChanged();

private:
    void load();
    void persist();
    static QString sessionsPath();

    CanDataModel  *m_canModel     = nullptr;
    RaceBoxModel  *m_raceBoxModel = nullptr;
    TrackModel    *m_trackModel   = nullptr;

    QVariantList   m_sessions;

    QList<qint64>       m_currentLapTimes;
    QList<QVariantList> m_currentLapPaths;
    int            m_topSpeedKmh = 0;
    double         m_maxOilC     = 0.0;
    double         m_maxCoolantC = 0.0;
};

#pragma once

#include <QObject>
#include <QVariantList>
#include <QList>

class CanDataModel;
class RaceBoxModel;

class SessionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(bool hasLaps READ hasLaps NOTIFY hasLapsChanged)

public:
    explicit SessionModel(CanDataModel *canModel, RaceBoxModel *raceBoxModel,
                          QObject *parent = nullptr);

    const QVariantList &sessions() const { return m_sessions; }
    bool hasLaps() const { return !m_currentLapTimes.isEmpty(); }

    Q_INVOKABLE void saveCurrentSession();

signals:
    void sessionsChanged();
    void hasLapsChanged();

private slots:
    void onLapCompleted(qint64 ms);
    void onSpeedChanged();
    void onOilTempChanged();
    void onCoolantTempChanged();

private:
    void load();
    void persist();
    static QString sessionsPath();

    CanDataModel  *m_canModel     = nullptr;
    RaceBoxModel  *m_raceBoxModel = nullptr;

    QVariantList   m_sessions;

    QList<qint64>  m_currentLapTimes;
    int            m_topSpeedKmh = 0;
    double         m_maxOilC     = 0.0;
    double         m_maxCoolantC = 0.0;
};

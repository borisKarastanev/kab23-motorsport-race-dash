#pragma once

#include "src/race/iraceprovider.h"
#include <QTimer>

class MockRaceBoxProvider : public IRaceBoxProvider {
    Q_OBJECT
public:
    explicit MockRaceBoxProvider(QObject *parent = nullptr);

    void start() override;
    void stop()  override;

    // Returns the finish-line gate (two endpoints A→B) for this mock track,
    // perpendicular to the circular path at the phase=0 crossing point.
    static void defaultFinishLine(double &latA, double &lonA, double &latB, double &lonB);

private slots:
    void onTick();

private:
    static constexpr double kTrackLat    = 51.5074;
    static constexpr double kTrackLon    = -0.1278;
    static constexpr double kTrackRadius = 0.00135; // ~150 m in degrees

    QTimer  m_timer;
    quint64 m_tick          = 0;
    int     m_currentLap    = 0; // 0-based; stops after kLapCount
    quint64 m_lapStartTick  = 0;
    bool    m_stopped       = false; // true after laps complete — emits speed=0
};

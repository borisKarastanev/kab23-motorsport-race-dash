#pragma once

#include <QObject>

class DashConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(int ledCount        READ ledCount        CONSTANT)
    Q_PROPERTY(int flashIntervalMs READ flashIntervalMs CONSTANT)
    // Pair thresholds, outside-in: pair 0 = outermost LEDs, pair 4 = centre pair
    Q_PROPERTY(int pair0Rpm  READ pair0Rpm  CONSTANT)
    Q_PROPERTY(int pair1Rpm  READ pair1Rpm  CONSTANT)
    Q_PROPERTY(int pair2Rpm  READ pair2Rpm  CONSTANT)
    Q_PROPERTY(int pair3Rpm  READ pair3Rpm  CONSTANT)
    Q_PROPERTY(int pair4Rpm  READ pair4Rpm  CONSTANT)
    Q_PROPERTY(int allBlueRpm  READ allBlueRpm  CONSTANT)
    Q_PROPERTY(int limiterRpm  READ limiterRpm  CONSTANT)

public:
    explicit DashConfig(QObject *parent = nullptr);

    int ledCount()        const { return m_ledCount; }
    int flashIntervalMs() const { return m_flashIntervalMs; }
    int pair0Rpm()        const { return m_pair0Rpm; }
    int pair1Rpm()        const { return m_pair1Rpm; }
    int pair2Rpm()        const { return m_pair2Rpm; }
    int pair3Rpm()        const { return m_pair3Rpm; }
    int pair4Rpm()        const { return m_pair4Rpm; }
    int allBlueRpm()      const { return m_allBlueRpm; }
    int limiterRpm()      const { return m_limiterRpm; }

private:
    int m_ledCount        = 10;
    int m_flashIntervalMs = 80;
    int m_pair0Rpm        = 5800;
    int m_pair1Rpm        = 6000;
    int m_pair2Rpm        = 6200;
    int m_pair3Rpm        = 6500;
    int m_pair4Rpm        = 6600;
    int m_allBlueRpm      = 6750;
    int m_limiterRpm      = 6800;
};

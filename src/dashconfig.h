#pragma once

#include <QObject>
#include <QString>

class DashConfig : public QObject {
    Q_OBJECT

    // --- LED strip -----------------------------------------------------------
    Q_PROPERTY(int ledCount        READ ledCount        CONSTANT)
    Q_PROPERTY(int flashIntervalMs READ flashIntervalMs CONSTANT)
    Q_PROPERTY(int pair0Rpm        READ pair0Rpm        CONSTANT)
    Q_PROPERTY(int pair1Rpm        READ pair1Rpm        CONSTANT)
    Q_PROPERTY(int pair2Rpm        READ pair2Rpm        CONSTANT)
    Q_PROPERTY(int pair3Rpm        READ pair3Rpm        CONSTANT)
    Q_PROPERTY(int pair4Rpm        READ pair4Rpm        CONSTANT)
    Q_PROPERTY(int allBlueRpm      READ allBlueRpm      CONSTANT)
    Q_PROPERTY(int limiterRpm      READ limiterRpm      CONSTANT)

    // --- Gauge visibility & position (left | center | right | bottom) --------
    Q_PROPERTY(bool    gearVisible     READ gearVisible     CONSTANT)
    Q_PROPERTY(bool    rpmVisible      READ rpmVisible      CONSTANT)
    Q_PROPERTY(QString rpmPosition     READ rpmPosition     CONSTANT)
    Q_PROPERTY(bool    speedVisible    READ speedVisible    CONSTANT)
    Q_PROPERTY(QString speedPosition   READ speedPosition   CONSTANT)
    Q_PROPERTY(bool    coolantVisible  READ coolantVisible  CONSTANT)
    Q_PROPERTY(QString coolantPosition READ coolantPosition CONSTANT)
    Q_PROPERTY(bool    oilTempVisible  READ oilTempVisible  CONSTANT)
    Q_PROPERTY(QString oilTempPosition READ oilTempPosition CONSTANT)
    Q_PROPERTY(bool    lapTimerVisible READ lapTimerVisible CONSTANT)

    // --- RaceBox BLE ---------------------------------------------------------
    Q_PROPERTY(QString raceBoxDeviceName  READ raceBoxDeviceName  CONSTANT)
    Q_PROPERTY(double  finishLineLat      READ finishLineLat      CONSTANT)
    Q_PROPERTY(double  finishLineLon      READ finishLineLon      CONSTANT)
    Q_PROPERTY(double  finishLineRadiusM  READ finishLineRadiusM  CONSTANT)

public:
    explicit DashConfig(QObject *parent = nullptr);

    int     ledCount()        const { return m_ledCount; }
    int     flashIntervalMs() const { return m_flashIntervalMs; }
    int     pair0Rpm()        const { return m_pair0Rpm; }
    int     pair1Rpm()        const { return m_pair1Rpm; }
    int     pair2Rpm()        const { return m_pair2Rpm; }
    int     pair3Rpm()        const { return m_pair3Rpm; }
    int     pair4Rpm()        const { return m_pair4Rpm; }
    int     allBlueRpm()      const { return m_allBlueRpm; }
    int     limiterRpm()      const { return m_limiterRpm; }

    bool    gearVisible()     const { return m_gearVisible; }
    bool    rpmVisible()      const { return m_rpmVisible; }
    QString rpmPosition()     const { return m_rpmPosition; }
    bool    speedVisible()    const { return m_speedVisible; }
    QString speedPosition()   const { return m_speedPosition; }
    bool    coolantVisible()  const { return m_coolantVisible; }
    QString coolantPosition() const { return m_coolantPosition; }
    bool    oilTempVisible()     const { return m_oilTempVisible; }
    QString oilTempPosition()    const { return m_oilTempPosition; }
    bool    lapTimerVisible()    const { return m_lapTimerVisible; }
    QString raceBoxDeviceName()  const { return m_raceBoxDeviceName; }
    double  finishLineLat()      const { return m_finishLineLat; }
    double  finishLineLon()      const { return m_finishLineLon; }
    double  finishLineRadiusM()  const { return m_finishLineRadiusM; }

private:
    int     m_ledCount        = 10;
    int     m_flashIntervalMs = 80;
    int     m_pair0Rpm        = 5800;
    int     m_pair1Rpm        = 6000;
    int     m_pair2Rpm        = 6200;
    int     m_pair3Rpm        = 6500;
    int     m_pair4Rpm        = 6600;
    int     m_allBlueRpm      = 6750;
    int     m_limiterRpm      = 6800;

    bool    m_gearVisible     = false;
    bool    m_rpmVisible      = true;
    QString m_rpmPosition     = "left";
    bool    m_speedVisible      = true;
    QString m_speedPosition     = "center";
    bool    m_coolantVisible    = true;
    QString m_coolantPosition   = "right";
    bool    m_oilTempVisible    = true;
    QString m_oilTempPosition   = "right";
    bool    m_lapTimerVisible   = true;

    QString m_raceBoxDeviceName = "RaceBox Mini";
    double  m_finishLineLat     = 0.0;
    double  m_finishLineLon     = 0.0;
    double  m_finishLineRadiusM = 20.0;
};

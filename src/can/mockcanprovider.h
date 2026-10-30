#pragma once

#include "src/can/icanprovider.h"
#include <QTimer>

class MockCanProvider : public ICanProvider {
    Q_OBJECT
public:
    explicit MockCanProvider(QObject *parent = nullptr);

    void start() override;
    void stop() override;

private slots:
    void onTick();

private:
    QTimer  m_timer;
    quint64 m_tick = 0;
    int     m_prevRpm = 0;
    bool    m_oilPressureLow = false; // MOCK_OIL_PRESSURE_LOW — preview the emergency warning
    bool    m_absFault = false;       // MOCK_ABS_FAULT — preview the red ABS lamp
};

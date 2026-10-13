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
};

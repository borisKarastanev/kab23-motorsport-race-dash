#pragma once

#include "icanprovider.h"
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

    static QCanBusFrame rpmFrame(int rpm);
    static QCanBusFrame tempFrame(double coolant, double oil, int speed);
    static QCanBusFrame gearFrame(int gear);
};

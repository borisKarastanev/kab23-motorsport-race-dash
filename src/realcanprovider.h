#pragma once

#include "icanprovider.h"
#include <QCanBusDevice>

class RealCanProvider : public ICanProvider {
    Q_OBJECT
public:
    explicit RealCanProvider(QObject *parent = nullptr);
    ~RealCanProvider() override;

    void start() override;
    void stop() override;

private slots:
    void onFramesReceived();
    void onErrorOccurred(QCanBusDevice::CanBusError error);

private:
    QCanBusDevice *m_device = nullptr;
};

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

private:
    void onFramesReceived();
    void onErrorOccurred(QCanBusDevice::CanBusError error);
    void scheduleRetry(const QString &reason);

private:
    QCanBusDevice *m_device           = nullptr;
    bool           m_firstFrameLogged = false;
    bool           m_retryScheduled   = false;
};

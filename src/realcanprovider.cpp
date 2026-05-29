#include "realcanprovider.h"
#include <QCanBus>
#include <QDebug>

static constexpr char kPlugin[]    = "socketcan";
static constexpr char kInterface[] = "can0";

RealCanProvider::RealCanProvider(QObject *parent)
    : ICanProvider(parent) {}

RealCanProvider::~RealCanProvider()
{
    stop();
}

void RealCanProvider::start()
{
    QString errorString;
    m_device = QCanBus::instance()->createDevice(kPlugin, kInterface, &errorString);
    if (!m_device) {
        qWarning() << "RealCanProvider: failed to create device:" << errorString;
        return;
    }

    connect(m_device, &QCanBusDevice::framesReceived, this, &RealCanProvider::onFramesReceived);
    connect(m_device, &QCanBusDevice::errorOccurred,  this, &RealCanProvider::onErrorOccurred);

    if (!m_device->connectDevice()) {
        qWarning() << "RealCanProvider: failed to connect:" << m_device->errorString();
        delete m_device;
        m_device = nullptr;
    }
}

void RealCanProvider::stop()
{
    if (m_device) {
        m_device->disconnectDevice();
        delete m_device;
        m_device = nullptr;
    }
}

void RealCanProvider::onFramesReceived()
{
    const QList<QCanBusFrame> frames = m_device->readAllFrames();
    for (const QCanBusFrame &f : frames)
        emit frameReady(f);
}

void RealCanProvider::onErrorOccurred(QCanBusDevice::CanBusError error)
{
    qWarning() << "RealCanProvider: CAN error:" << error << m_device->errorString();
}

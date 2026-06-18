#include "realcanprovider.h"
#include "logging.h"
#include <QCanBus>

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
        qCWarning(lcCan) << "Failed to create SocketCAN device:" << errorString;
        return;
    }

    connect(m_device, &QCanBusDevice::framesReceived, this, &RealCanProvider::onFramesReceived);
    connect(m_device, &QCanBusDevice::errorOccurred,  this, &RealCanProvider::onErrorOccurred);

    if (!m_device->connectDevice()) {
        qCWarning(lcCan) << "Failed to connect to" << kInterface << "—" << m_device->errorString();
        delete m_device;
        m_device = nullptr;
        return;
    }

    qCInfo(lcCan) << "Connected to" << kInterface;
}

void RealCanProvider::stop()
{
    if (m_device) {
        m_device->disconnectDevice();
        qCInfo(lcCan) << "Disconnected from" << kInterface;
        delete m_device;
        m_device = nullptr;
    }
}

void RealCanProvider::onFramesReceived()
{
    const QList<QCanBusFrame> frames = m_device->readAllFrames();
    if (!m_firstFrameLogged && !frames.isEmpty()) {
        qCInfo(lcCan) << "First frame received — CAN bus active";
        m_firstFrameLogged = true;
    }
    for (const QCanBusFrame &f : frames)
        emit frameReady(f);
}

void RealCanProvider::onErrorOccurred(QCanBusDevice::CanBusError error)
{
    qCWarning(lcCan) << "CAN error" << error << "—" << m_device->errorString();
}

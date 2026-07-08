#include "realcanprovider.h"
#include "logging.h"
#include <QCanBus>
#include <QTimer>

static constexpr char kPlugin[]      = "socketcan";
static constexpr char kInterface[]   = "can0";
static constexpr int  kRetryDelayMs  = 3000;

RealCanProvider::RealCanProvider(QObject *parent)
    : ICanProvider(parent) {}

RealCanProvider::~RealCanProvider()
{
    stop();
}

void RealCanProvider::start()
{
    if (m_device)
        return; // already connected/connecting — ignore a duplicate start()

    QString errorString;
    m_device = QCanBus::instance()->createDevice(kPlugin, kInterface, &errorString);
    if (!m_device) {
        scheduleRetry("Failed to create SocketCAN device: " + errorString);
        return;
    }

    connect(m_device, &QCanBusDevice::framesReceived, this, &RealCanProvider::onFramesReceived);
    connect(m_device, &QCanBusDevice::errorOccurred,  this, &RealCanProvider::onErrorOccurred);

    if (!m_device->connectDevice()) {
        const QString err = m_device->errorString();
        delete m_device;
        m_device = nullptr;
        scheduleRetry("Failed to connect to " + QString(kInterface) + " — " + err);
        return;
    }

    qCInfo(lcCan) << "Connected to" << kInterface;
}

void RealCanProvider::scheduleRetry(const QString &reason)
{
    // Guard against stacking timers: start() only reaches here when m_device is
    // null, and each retry chain schedules exactly one singleShot that calls
    // start() again — but if something external ever calls start() again while
    // a retry is already pending, this flag stops a second chain from forming.
    if (m_retryScheduled)
        return;
    m_retryScheduled = true;

    qCWarning(lcCan) << reason << "— retrying in" << kRetryDelayMs << "ms";
    // can0 may not exist yet at boot (USB2CAN adapter enumeration, slcand not
    // up yet) — retry instead of giving up permanently, mirroring the BLE
    // adapter-not-ready retry in RaceBoxProvider::onScanError(). Using `this`
    // as the context object means Qt automatically drops this callback if the
    // provider is destroyed before the timer fires — no dangling access.
    QTimer::singleShot(kRetryDelayMs, this, [this]() {
        m_retryScheduled = false;
        start();
    });
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

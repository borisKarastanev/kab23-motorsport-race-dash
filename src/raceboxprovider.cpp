#ifdef HAVE_BLUETOOTH

#include "raceboxprovider.h"
#include <QtEndian>
#include <QTimer>
#include <QDebug>

// Nordic UART service
const QBluetoothUuid RaceBoxProvider::kUartServiceUuid{
    QStringLiteral("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")};
// TX characteristic — device sends data here; client subscribes for notifications
const QBluetoothUuid RaceBoxProvider::kTxCharUuid{
    QStringLiteral("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")};

// UBX packet constants
static constexpr quint8  UBX_SYNC1    = 0xB5;
static constexpr quint8  UBX_SYNC2    = 0x62;
static constexpr quint8  UBX_CLASS    = 0xFF;
static constexpr quint8  UBX_ID       = 0x01;
static constexpr int     UBX_PAYLOAD  = 80;
static constexpr int     UBX_OVERHEAD = 8; // 2 sync + 2 class/id + 2 len + 2 checksum
static constexpr int     UBX_TOTAL    = UBX_OVERHEAD + UBX_PAYLOAD;

RaceBoxProvider::RaceBoxProvider(const QString &deviceNamePrefix, QObject *parent)
    : IRaceBoxProvider(parent)
    , m_deviceNamePrefix(deviceNamePrefix)
{}

void RaceBoxProvider::start()
{
    QBluetoothLocalDevice localDevice;
    if (localDevice.isValid() &&
        localDevice.hostMode() == QBluetoothLocalDevice::HostPoweredOff)
        localDevice.setHostMode(QBluetoothLocalDevice::HostConnectable);

    m_scanner = new QBluetoothDeviceDiscoveryAgent(this);
    m_scanner->setLowEnergyDiscoveryTimeout(10000);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &RaceBoxProvider::onDeviceDiscovered);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &RaceBoxProvider::onScanFinished);
    connect(m_scanner, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &RaceBoxProvider::onScanError);
    qDebug() << "[RaceBox] Scanning for" << m_deviceNamePrefix;
    m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void RaceBoxProvider::stop()
{
    if (m_scanner) m_scanner->stop();
    if (m_ctrl)    m_ctrl->disconnectFromDevice();
}

void RaceBoxProvider::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    if (!info.name().startsWith(m_deviceNamePrefix))
        return;
    qDebug() << "[RaceBox] Found device:" << info.name() << info.address().toString();
    m_scanner->stop();
    connectToDevice(info);
}

void RaceBoxProvider::onScanFinished()
{
    if (!m_ctrl)
        QTimer::singleShot(2000, this, [this]() {
            if (!m_ctrl) {
                qDebug() << "[RaceBox] Scan finished, no device — restarting scan";
                m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
            }
        });
}

void RaceBoxProvider::onScanError(QBluetoothDeviceDiscoveryAgent::Error err)
{
    qDebug() << "[RaceBox] Scan error" << err << "— retrying in 5 s";
    if (!m_ctrl)
        QTimer::singleShot(5000, this, [this]() {
            if (!m_ctrl) m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
        });
}

void RaceBoxProvider::connectToDevice(const QBluetoothDeviceInfo &info)
{
    m_ctrl = QLowEnergyController::createCentral(info, this);
    connect(m_ctrl, &QLowEnergyController::connected,
            this, &RaceBoxProvider::onConnected);
    connect(m_ctrl, &QLowEnergyController::disconnected,
            this, &RaceBoxProvider::onDisconnected);
    connect(m_ctrl, &QLowEnergyController::discoveryFinished,
            this, &RaceBoxProvider::onAllServicesDiscovered);
    connect(m_ctrl, &QLowEnergyController::errorOccurred,
            this, [](QLowEnergyController::Error err) {
                qDebug() << "[RaceBox] Controller error:" << err;
            });
    qDebug() << "[RaceBox] Connecting to device…";
    m_ctrl->connectToDevice();
}

void RaceBoxProvider::onConnected()
{
    qDebug() << "[RaceBox] Connected — discovering services";
    emit connectionStateChanged(true);
    m_ctrl->discoverServices();
}

void RaceBoxProvider::onDisconnected()
{
    qDebug() << "[RaceBox] Disconnected — waiting 5 s for BlueZ cleanup before reconnect";
    emit connectionStateChanged(false);

    if (m_service) { m_service->deleteLater(); m_service = nullptr; }
    if (m_ctrl)    { m_ctrl->deleteLater();    m_ctrl    = nullptr; }
    m_buffer.clear();

    // Immediate reconnect reuses stale D-Bus GATT objects and causes InvalidService.
    // 5 seconds gives BlueZ time to fully tear down the previous connection.
    QTimer::singleShot(5000, this, [this]() {
        if (!m_ctrl && m_scanner)
            m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    });
}

// Called once the controller has finished enumerating ALL services.
// Only now is it safe to call discoverDetails() on a specific service.
void RaceBoxProvider::onAllServicesDiscovered()
{
    qDebug() << "[RaceBox] All services enumerated:" << m_ctrl->services();

    if (!m_ctrl->services().contains(kUartServiceUuid)) {
        qDebug() << "[RaceBox] UART service not found — disconnecting";
        m_ctrl->disconnectFromDevice();
        return;
    }

    qDebug() << "[RaceBox] UART service found — discovering details";
    m_service = m_ctrl->createServiceObject(kUartServiceUuid, this);
    connect(m_service, &QLowEnergyService::stateChanged,
            this, &RaceBoxProvider::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &RaceBoxProvider::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::errorOccurred,
            this, [](QLowEnergyService::ServiceError err) {
                qDebug() << "[RaceBox] Service error:" << err;
            });
    connect(m_service, &QLowEnergyService::descriptorWritten,
            this, [](const QLowEnergyDescriptor &d, const QByteArray &val) {
                qDebug() << "[RaceBox] Descriptor written:" << d.uuid() << val.toHex();
            });
    // SkipValueDiscovery: finds characteristics and descriptors without reading
    // their current values — significantly fewer BLE round-trips, less likely to
    // time out before the CCCD write.
    m_service->discoverDetails(QLowEnergyService::DiscoveryMode::SkipValueDiscovery);
}

void RaceBoxProvider::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    qDebug() << "[RaceBox] Service state:" << state;
    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    const QLowEnergyCharacteristic txChar = m_service->characteristic(kTxCharUuid);
    if (!txChar.isValid()) {
        qDebug() << "[RaceBox] TX characteristic not found — check UUID";
        return;
    }

    const QLowEnergyDescriptor cccd = txChar.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid()) {
        qDebug() << "[RaceBox] CCCD descriptor not found — notifications may not work";
        return;
    }

    qDebug() << "[RaceBox] Enabling TX notifications";
    m_service->writeDescriptor(cccd, QByteArray::fromHex("0100"));
}

void RaceBoxProvider::onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                               const QByteArray &value)
{
    Q_UNUSED(c)
    m_buffer.append(value);
    tryParsePacket();
}

void RaceBoxProvider::tryParsePacket()
{
    while (m_buffer.size() >= UBX_TOTAL) {
        const int syncPos = [&]() -> int {
            for (int i = 0; i <= m_buffer.size() - 2; ++i) {
                if (static_cast<quint8>(m_buffer[i])   == UBX_SYNC1 &&
                    static_cast<quint8>(m_buffer[i+1]) == UBX_SYNC2)
                    return i;
            }
            return -1;
        }();

        if (syncPos < 0) { m_buffer.clear(); return; }
        if (syncPos > 0) { m_buffer.remove(0, syncPos); }
        if (m_buffer.size() < UBX_TOTAL) return;

        if (static_cast<quint8>(m_buffer[2]) != UBX_CLASS ||
            static_cast<quint8>(m_buffer[3]) != UBX_ID) {
            m_buffer.remove(0, 2);
            continue;
        }
        const quint16 payLen = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar *>(m_buffer.constData() + 4));
        if (payLen != UBX_PAYLOAD) { m_buffer.remove(0, 2); continue; }

        quint8 ckA = 0, ckB = 0;
        for (int i = 2; i < 6 + UBX_PAYLOAD; ++i) {
            ckA += static_cast<quint8>(m_buffer[i]);
            ckB += ckA;
        }
        if (ckA != static_cast<quint8>(m_buffer[6 + UBX_PAYLOAD]) ||
            ckB != static_cast<quint8>(m_buffer[7 + UBX_PAYLOAD])) {
            qDebug() << "[RaceBox] Checksum mismatch — discarding 2 bytes";
            m_buffer.remove(0, 2);
            continue;
        }

        const uchar *p = reinterpret_cast<const uchar *>(m_buffer.constData() + 6);

        RaceBoxData d;
        d.fixStatus  = p[20];
        d.fixFlags   = p[21];
        d.numSvs     = p[23];
        d.longitude  = qFromLittleEndian<qint32>(p + 24) * 1e-7;
        d.latitude   = qFromLittleEndian<qint32>(p + 28) * 1e-7;
        d.speedMmS   = qFromLittleEndian<qint32>(p + 48);
        d.batteryRaw = p[67];
        d.gForceXMg  = qFromLittleEndian<qint16>(p + 68);
        d.gForceYMg  = qFromLittleEndian<qint16>(p + 70);
        d.gForceZMg  = qFromLittleEndian<qint16>(p + 72);

        qDebug() << "[RaceBox] Packet OK — fix:" << d.fixStatus
                 << "svs:" << d.numSvs
                 << "speed mm/s:" << d.speedMmS
                 << "battery raw:" << d.batteryRaw
                 << "(" << (d.batteryRaw & 0x7F) << "%" << ((d.batteryRaw & 0x80) ? "charging" : "") << ")";

        m_buffer.remove(0, UBX_TOTAL);
        emit dataReady(d);
    }
}

#endif // HAVE_BLUETOOTH

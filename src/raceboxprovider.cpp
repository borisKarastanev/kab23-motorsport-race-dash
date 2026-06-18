#ifdef HAVE_BLUETOOTH

#include "raceboxprovider.h"
#include "logging.h"
#include <QtEndian>
#include <QTimer>

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
    qCInfo(lcRaceBox) << "Scanning for" << m_deviceNamePrefix;
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
    qCInfo(lcRaceBox) << "Found" << info.name() << "— connecting";
    m_scanner->stop();
    connectToDevice(info);
}

void RaceBoxProvider::onScanFinished()
{
    if (!m_ctrl)
        QTimer::singleShot(2000, this, [this]() {
            if (!m_ctrl) m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
        });
}

void RaceBoxProvider::onScanError(QBluetoothDeviceDiscoveryAgent::Error err)
{
    // Adapter may not be ready at boot (race with bluetoothd startup) — retry after delay
    qCWarning(lcRaceBox) << "Scan error" << err << "— retrying in 5 s";
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
    m_ctrl->connectToDevice();
}

void RaceBoxProvider::onConnected()
{
    qCInfo(lcRaceBox) << "Connected — discovering services";
    emit connectionStateChanged(true);
    m_ctrl->discoverServices();
}

void RaceBoxProvider::onDisconnected()
{
    qCInfo(lcRaceBox) << "Disconnected — reconnecting in 5 s";
    emit connectionStateChanged(false);

    if (m_service) { m_service->deleteLater(); m_service = nullptr; }
    if (m_ctrl)    { m_ctrl->deleteLater();    m_ctrl    = nullptr; }
    m_buffer.clear();

    // Immediate reconnect reuses stale BlueZ D-Bus GATT objects and causes
    // InvalidService. 5 s gives BlueZ time to fully tear down the connection.
    QTimer::singleShot(5000, this, [this]() {
        if (!m_ctrl && m_scanner)
            m_scanner->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    });
}

void RaceBoxProvider::onAllServicesDiscovered()
{
    if (!m_ctrl->services().contains(kUartServiceUuid)) {
        qCWarning(lcRaceBox) << "UART service not found on device — disconnecting";
        m_ctrl->disconnectFromDevice();
        return;
    }

    m_service = m_ctrl->createServiceObject(kUartServiceUuid, this);
    connect(m_service, &QLowEnergyService::stateChanged,
            this, &RaceBoxProvider::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &RaceBoxProvider::onCharacteristicChanged);
    // SkipValueDiscovery: finds characteristics and descriptors without reading
    // their values — fewer BLE round-trips, faster than full discovery.
    m_service->discoverDetails(QLowEnergyService::DiscoveryMode::SkipValueDiscovery);
}

void RaceBoxProvider::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    const QLowEnergyCharacteristic txChar = m_service->characteristic(kTxCharUuid);
    if (!txChar.isValid()) {
        qCWarning(lcRaceBox) << "TX characteristic not found";
        return;
    }

    const QLowEnergyDescriptor cccd = txChar.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!cccd.isValid()) {
        qCWarning(lcRaceBox) << "CCCD not found — notifications unavailable";
        return;
    }

    qCInfo(lcRaceBox) << "TX notifications enabled";
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

        m_buffer.remove(0, UBX_TOTAL);
        emit dataReady(d);
    }
}

#endif // HAVE_BLUETOOTH

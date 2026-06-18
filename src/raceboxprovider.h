#pragma once

#ifdef HAVE_BLUETOOTH

#include "iraceprovider.h"
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QLowEnergyController>
#include <QLowEnergyService>

class RaceBoxProvider : public IRaceBoxProvider {
    Q_OBJECT
public:
    explicit RaceBoxProvider(const QString &deviceNamePrefix = "RaceBox Mini",
                             QObject *parent = nullptr);

    void start() override;
    void stop()  override;

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onDiscoveryFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);
    void onConnected();
    void onDisconnected();
    void onServiceDiscovered(const QBluetoothUuid &uuid);
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &c,
                                 const QByteArray &value);

private:
    void connectToDevice(const QBluetoothDeviceInfo &info);
    void tryParsePacket();

    QString                         m_deviceNamePrefix;
    QBluetoothDeviceDiscoveryAgent *m_scanner  = nullptr;
    QLowEnergyController           *m_ctrl     = nullptr;
    QLowEnergyService              *m_service  = nullptr;
    QByteArray                      m_buffer;

    // Nordic UART over BLE
    static const QBluetoothUuid kUartServiceUuid;
    static const QBluetoothUuid kTxCharUuid;
};

#endif // HAVE_BLUETOOTH

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QTimer>
#include <functional>

class QProcess;

// Wi-Fi + wired network status/control for the Settings > Device Settings >
// Network Connection pages. Wraps `nmcli` (NetworkManager) rather than
// implementing wireless handling directly — NetworkManager already persists
// saved credentials, autoconnect priority and the radio on/off state across
// reboots, so this model does no persistence of its own. No sudo is required:
// nmcli operates under the dashboard user's NetworkManager polkit permissions
// (see install.sh for the netdev group / polkit rule needed under the
// systemd service). See qml/settings/WifiDetail.qml and WiredDetail.qml.
class NetworkModel : public QObject {
    Q_OBJECT
public:
    // Drives WifiDetail.qml + WifiPasswordModal.qml UI while joining a network.
    enum ConnectState {
        Idle,
        NeedPassword,
        Connecting,
        WrongPassword,
        ConnectFailed
    };
    Q_ENUM(ConnectState)

    Q_PROPERTY(bool nmAvailable READ nmAvailable NOTIFY statusChanged)

    Q_PROPERTY(bool wifiEnabled READ wifiEnabled NOTIFY statusChanged)
    Q_PROPERTY(bool wifiConnected READ wifiConnected NOTIFY statusChanged)
    Q_PROPERTY(QString wifiSsid READ wifiSsid NOTIFY statusChanged)
    Q_PROPERTY(QString wifiIp READ wifiIp NOTIFY statusChanged)
    Q_PROPERTY(QString wifiSubnet READ wifiSubnet NOTIFY statusChanged)
    Q_PROPERTY(QString wifiGateway READ wifiGateway NOTIFY statusChanged)

    Q_PROPERTY(bool wiredConnected READ wiredConnected NOTIFY statusChanged)
    Q_PROPERTY(QString wiredIp READ wiredIp NOTIFY statusChanged)
    Q_PROPERTY(QString wiredSubnet READ wiredSubnet NOTIFY statusChanged)
    Q_PROPERTY(QString wiredGateway READ wiredGateway NOTIFY statusChanged)

    // List of QVariantMap { ssid, signal(int 0-100), secured(bool), known(bool) },
    // sorted by signal descending, active SSID excluded, hidden/duplicate SSIDs filtered.
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)

    Q_PROPERTY(ConnectState connectState READ connectState NOTIFY connectStateChanged)
    Q_PROPERTY(QString pendingSsid READ pendingSsid NOTIFY connectStateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY connectStateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY connectStateChanged)

    explicit NetworkModel(bool mockMode, QObject *parent = nullptr);

    bool nmAvailable() const { return m_nmAvailable; }

    bool wifiEnabled() const { return m_wifiEnabled; }
    bool wifiConnected() const { return m_wifiConnected; }
    QString wifiSsid() const { return m_wifiSsid; }
    QString wifiIp() const { return m_wifiIp; }
    QString wifiSubnet() const { return m_wifiSubnet; }
    QString wifiGateway() const { return m_wifiGateway; }

    bool wiredConnected() const { return m_wiredConnected; }
    QString wiredIp() const { return m_wiredIp; }
    QString wiredSubnet() const { return m_wiredSubnet; }
    QString wiredGateway() const { return m_wiredGateway; }

    QVariantList networks() const { return m_networks; }

    ConnectState connectState() const { return m_connectState; }
    QString pendingSsid() const { return m_pendingSsid; }
    QString errorText() const { return m_errorText; }
    bool busy() const { return m_toggleBusy || m_connectState == Connecting; }

    // Refcounted page-open gating: a StackView can keep NetworkMenu alive
    // underneath WifiDetail/WiredDetail, so a plain bool would be cleared
    // too early when the detail page pops while the menu is still visible.
    Q_INVOKABLE void acquire();
    Q_INVOKABLE void release();

    Q_INVOKABLE void setWifiEnabled(bool on);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void connectToNetwork(const QString &ssid);
    Q_INVOKABLE void submitPassword(const QString &password);
    Q_INVOKABLE void cancelConnect();

signals:
    void statusChanged();
    void networksChanged();
    void connectStateChanged();

private:
    void setConnectState(ConnectState s, const QString &error = QString());
    bool isNetworkSecured(const QString &ssid) const;

    // async nmcli runner: no sudo, MergedChannels, timeout kill, deleteLater.
    void runNmcli(const QStringList &args, int timeoutMs,
                  std::function<void(bool ok, int exitCode, const QString &out)> onDone);

    // real (unmocked) poll chain
    void checkNmAvailable();
    void pollTick();
    void refreshRadioAndStatus();
    void refreshDevStatus();
    void refreshIpDetails();
    void refreshDeviceIp(const QString &device, std::function<void(QString ip, QString mask, QString gw)> cb);
    void refreshDeviceIpInto(bool connected, const QString &device,
                             QString &ipField, QString &maskField, QString &gwField,
                             std::function<void()> next);
    void maybeRefreshScan();
    void refreshScanList();
    void refreshKnownProfiles();
    void finishPollCycle();
    void handleConnectResult(const QString &ssid, bool ok, const QString &output);
    void bumpAutoconnectPriority(const QString &ssid);

    void parseDevStatus(const QString &output);
    void parseScanList(const QString &output);
    void parseKnownProfiles(const QString &output);
    static void parseDevShowIp(const QString &output, QString &ip, QString &mask, QString &gw);
    static QStringList splitTerseLine(const QString &line);
    static QString prefixToMask(int prefix);
    static QString lastNonEmptyLine(const QString &text);

    // mock mode
    void mockInit();
    void mockSetConnected(const QString &ssid);
    void rebuildMockNetworks(const QString &activeSsid);

    bool m_mockMode;
    bool m_nmAvailable = false;

    bool m_wifiEnabled = false;
    bool m_wifiConnected = false;
    QString m_wifiSsid;
    QString m_wifiIp, m_wifiSubnet, m_wifiGateway;

    bool m_wiredConnected = false;
    QString m_wiredIp, m_wiredSubnet, m_wiredGateway;

    QVariantList m_networks;
    QStringList m_knownSsids;

    ConnectState m_connectState = Idle;
    QString m_pendingSsid;
    QString m_errorText;
    bool m_toggleBusy = false;

    QString m_wifiDev, m_wiredDev;

    QTimer m_timer;
    int m_activeRefs = 0;
    bool m_pollInFlight = false;
    int m_scanTick = 0;
    bool m_forceScanOnAcquire = false;

    static constexpr int kBackgroundPollMs = 10000;
    static constexpr int kActivePollMs = 3000;
    static constexpr int kScanEveryNTicks = 7; // ~20s at the 3s active poll rate
};

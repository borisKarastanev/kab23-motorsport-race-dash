#include "networkmodel.h"
#include "logging.h"

#include <QProcess>
#include <QDateTime>
#include <QMap>
#include <QList>
#include <QVector>
#include <algorithm>
#include <memory>

namespace {

const QVector<QVariantMap> kMockPool = {
    QVariantMap{{"ssid", "PitLane-WiFi"},  {"signal", 88}, {"secured", true}},
    QVariantMap{{"ssid", "Paddock-Guest"}, {"signal", 78}, {"secured", false}},
    QVariantMap{{"ssid", "TrackOps-5G"},   {"signal", 62}, {"secured", true}},
    QVariantMap{{"ssid", "Marshals"},      {"signal", 45}, {"secured", true}},
};

constexpr const char *kMockPassword = "password123";

}

NetworkModel::NetworkModel(bool mockMode, QObject *parent)
    : QObject(parent)
    , m_mockMode(mockMode)
{
    m_timer.setInterval(kBackgroundPollMs);
    connect(&m_timer, &QTimer::timeout, this, &NetworkModel::pollTick);

    if (m_mockMode) {
        mockInit();
    } else {
        checkNmAvailable();
    }

    m_timer.start();
}

// ── page-open gating ─────────────────────────────────────────────────

void NetworkModel::acquire()
{
    m_activeRefs++;
    if (m_activeRefs == 1) {
        m_scanTick = 0;
        m_forceScanOnAcquire = true;
        m_timer.start(kActivePollMs);
        pollTick();
    }
}

void NetworkModel::release()
{
    if (m_activeRefs > 0)
        m_activeRefs--;
    if (m_activeRefs == 0)
        m_timer.start(kBackgroundPollMs);
}

// ── property helpers ─────────────────────────────────────────────────

void NetworkModel::setConnectState(ConnectState s, const QString &error)
{
    m_connectState = s;
    m_errorText = error;
    emit connectStateChanged();
}

bool NetworkModel::isNetworkSecured(const QString &ssid) const
{
    for (const QVariant &v : m_networks) {
        const QVariantMap m = v.toMap();
        if (m.value("ssid").toString() == ssid)
            return m.value("secured").toBool();
    }
    return false;
}

// ── Q_INVOKABLE actions ──────────────────────────────────────────────

void NetworkModel::setWifiEnabled(bool on)
{
    if (m_toggleBusy)
        return;
    m_toggleBusy = true;
    emit connectStateChanged();

    if (m_mockMode) {
        QTimer::singleShot(400, this, [this, on]() {
            m_wifiEnabled = on;
            if (!on) {
                m_wifiConnected = false;
                m_wifiSsid.clear();
                m_wifiIp.clear();
                m_wifiSubnet.clear();
                m_wifiGateway.clear();
                rebuildMockNetworks(QString());
            } else {
                mockSetConnected("PitLane-WiFi");
            }
            m_toggleBusy = false;
            emit statusChanged();
            emit connectStateChanged();
        });
        return;
    }

    runNmcli({"radio", "wifi", on ? "on" : "off"}, 10000,
        [this](bool, int, const QString &) {
            m_toggleBusy = false;
            emit connectStateChanged();
            pollTick();
        });
}

void NetworkModel::rescan()
{
    if (m_mockMode) {
        emit networksChanged();
        return;
    }
    if (!m_nmAvailable)
        return;
    refreshScanList();
}

void NetworkModel::connectToNetwork(const QString &ssid)
{
    if (m_connectState == Connecting)
        return;

    const bool known = m_knownSsids.contains(ssid);
    const bool secured = isNetworkSecured(ssid);
    m_pendingSsid = ssid;

    if (m_mockMode) {
        if (secured && !known) {
            setConnectState(NeedPassword);
            return;
        }
        setConnectState(Connecting);
        QTimer::singleShot(1200, this, [this, ssid]() {
            mockSetConnected(ssid);
            m_pendingSsid.clear();
            setConnectState(Idle);
            emit statusChanged();
        });
        return;
    }

    if (known) {
        setConnectState(Connecting);
        runNmcli({"-w", "25", "con", "up", "id", ssid}, 30000,
            [this, ssid](bool ok, int, const QString &out) { handleConnectResult(ssid, ok, out); });
    } else if (secured) {
        setConnectState(NeedPassword);
    } else {
        setConnectState(Connecting);
        runNmcli({"-w", "25", "dev", "wifi", "connect", ssid}, 30000,
            [this, ssid](bool ok, int, const QString &out) { handleConnectResult(ssid, ok, out); });
    }
}

void NetworkModel::submitPassword(const QString &password)
{
    if (m_connectState != NeedPassword && m_connectState != WrongPassword)
        return;

    const QString ssid = m_pendingSsid;
    setConnectState(Connecting);

    if (m_mockMode) {
        QTimer::singleShot(1200, this, [this, ssid, password]() {
            if (password != QLatin1String(kMockPassword)) {
                setConnectState(WrongPassword, "Incorrect password");
                return;
            }
            mockSetConnected(ssid);
            m_pendingSsid.clear();
            setConnectState(Idle);
            emit statusChanged();
        });
        return;
    }

    runNmcli({"-w", "25", "dev", "wifi", "connect", ssid, "password", password}, 30000,
        [this, ssid](bool ok, int, const QString &out) { handleConnectResult(ssid, ok, out); });
}

void NetworkModel::cancelConnect()
{
    if (m_connectState != NeedPassword && m_connectState != WrongPassword)
        return;
    m_pendingSsid.clear();
    setConnectState(Idle);
}

// ── connect result handling (real mode) ──────────────────────────────

void NetworkModel::handleConnectResult(const QString &ssid, bool ok, const QString &output)
{
    if (ok) {
        m_pendingSsid.clear();
        setConnectState(Idle);
        bumpAutoconnectPriority(ssid);
        pollTick();
        return;
    }

    const bool secretsError = output.contains("Secrets were required", Qt::CaseInsensitive)
                            || output.contains("802-1X", Qt::CaseInsensitive)
                            || output.contains("no secrets", Qt::CaseInsensitive);

    if (secretsError) {
        // Drop the broken profile so the next attempt re-prompts for a
        // password instead of silently retrying a bad saved PSK.
        runNmcli({"con", "delete", "id", ssid}, 8000,
            [this, ssid](bool, int, const QString &) {
                m_knownSsids.removeAll(ssid);
                m_pendingSsid = ssid;
                setConnectState(WrongPassword, "Incorrect password");
            });
    } else {
        setConnectState(ConnectFailed, lastNonEmptyLine(output));
        QTimer::singleShot(4000, this, [this]() {
            if (m_connectState == ConnectFailed)
                setConnectState(Idle);
        });
    }
}

void NetworkModel::bumpAutoconnectPriority(const QString &ssid)
{
    if (ssid.isEmpty())
        return;
    const qint64 priority = QDateTime::currentSecsSinceEpoch() / 60;
    runNmcli({"con", "modify", "id", ssid,
              "connection.autoconnect", "yes",
              "connection.autoconnect-priority", QString::number(priority)},
        8000, [](bool, int, const QString &) {});
}

// ── real-mode poll chain ─────────────────────────────────────────────

void NetworkModel::checkNmAvailable()
{
    runNmcli({"-t", "-f", "RUNNING", "general", "status"}, 5000,
        [this](bool ok, int, const QString &out) {
            m_nmAvailable = ok && out.trimmed().compare("running", Qt::CaseInsensitive) == 0;
            if (!m_nmAvailable)
                qCInfo(lcApp) << "NetworkManager not available — Network Connection settings disabled";
            emit statusChanged();
            if (m_nmAvailable)
                pollTick();
        });
}

void NetworkModel::pollTick()
{
    if (m_mockMode || !m_nmAvailable || m_pollInFlight || m_connectState == Connecting)
        return;
    m_pollInFlight = true;
    // The wifi radio on/off state only feeds WifiDetail's toggle, so skip that
    // query in the background — the StatusBar icon needs only the connected
    // state, which comes from `dev status`.
    if (m_activeRefs > 0)
        refreshRadioAndStatus();
    else
        refreshDevStatus();
}

void NetworkModel::refreshRadioAndStatus()
{
    runNmcli({"-t", "radio", "wifi"}, 5000,
        [this](bool ok, int, const QString &out) {
            if (ok)
                m_wifiEnabled = out.trimmed().compare("enabled", Qt::CaseInsensitive) == 0;
            refreshDevStatus();
        });
}

void NetworkModel::refreshDevStatus()
{
    runNmcli({"-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "dev", "status"}, 5000,
        [this](bool ok, int, const QString &out) {
            if (ok)
                parseDevStatus(out);

            if (m_activeRefs > 0)
                refreshIpDetails();
            else
                finishPollCycle();
        });
}

void NetworkModel::refreshIpDetails()
{
    // Fetch the wifi device's IP, then the wired device's, then move on to the
    // scan step. Fields target stable members, so the async callbacks binding
    // them by reference are safe for `this`'s lifetime.
    refreshDeviceIpInto(m_wifiConnected, m_wifiDev, m_wifiIp, m_wifiSubnet, m_wifiGateway, [this]() {
        refreshDeviceIpInto(m_wiredConnected, m_wiredDev, m_wiredIp, m_wiredSubnet, m_wiredGateway, [this]() {
            maybeRefreshScan();
        });
    });
}

void NetworkModel::refreshDeviceIpInto(bool connected, const QString &device,
                                        QString &ipField, QString &maskField, QString &gwField,
                                        std::function<void()> next)
{
    if (connected && !device.isEmpty()) {
        refreshDeviceIp(device, [&ipField, &maskField, &gwField, next](QString ip, QString mask, QString gw) {
            ipField = ip;
            maskField = mask;
            gwField = gw;
            next();
        });
    } else {
        ipField.clear();
        maskField.clear();
        gwField.clear();
        next();
    }
}

void NetworkModel::refreshDeviceIp(const QString &device,
                                    std::function<void(QString ip, QString mask, QString gw)> cb)
{
    runNmcli({"-t", "-f", "IP4.ADDRESS,IP4.GATEWAY", "dev", "show", device}, 5000,
        [cb](bool ok, int, const QString &out) {
            QString ip, mask, gw;
            if (ok)
                parseDevShowIp(out, ip, mask, gw);
            cb(ip, mask, gw);
        });
}

void NetworkModel::maybeRefreshScan()
{
    m_scanTick++;
    const bool due = m_forceScanOnAcquire || (m_scanTick % kScanEveryNTicks == 0);
    m_forceScanOnAcquire = false;

    if (due)
        refreshScanList();
    else
        finishPollCycle();
}

void NetworkModel::refreshScanList()
{
    runNmcli({"dev", "wifi", "rescan"}, 8000,
        [this](bool, int, const QString &) {
            runNmcli({"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "dev", "wifi", "list"}, 8000,
                [this](bool ok, int, const QString &out) {
                    if (ok)
                        parseScanList(out);
                    refreshKnownProfiles();
                });
        });
}

void NetworkModel::refreshKnownProfiles()
{
    runNmcli({"-t", "-f", "NAME,TYPE", "con", "show"}, 5000,
        [this](bool ok, int, const QString &out) {
            if (ok)
                parseKnownProfiles(out);
            emit networksChanged();
            finishPollCycle();
        });
}

void NetworkModel::finishPollCycle()
{
    m_pollInFlight = false;
    emit statusChanged();
}

// ── parsing ───────────────────────────────────────────────────────────

QStringList NetworkModel::splitTerseLine(const QString &line)
{
    QStringList fields;
    QString cur;
    bool escape = false;
    for (const QChar c : line) {
        if (escape) {
            cur += c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == ':') {
            fields << cur;
            cur.clear();
            continue;
        }
        cur += c;
    }
    fields << cur;
    return fields;
}

QString NetworkModel::prefixToMask(int prefix)
{
    quint32 mask;
    if (prefix <= 0)
        mask = 0;
    else if (prefix >= 32)
        mask = 0xFFFFFFFFu;
    else
        mask = ~((1u << (32 - prefix)) - 1);

    return QStringLiteral("%1.%2.%3.%4")
        .arg((mask >> 24) & 0xFF).arg((mask >> 16) & 0xFF)
        .arg((mask >> 8) & 0xFF).arg(mask & 0xFF);
}

QString NetworkModel::lastNonEmptyLine(const QString &text)
{
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

void NetworkModel::parseDevStatus(const QString &output)
{
    QString wifiDev, wiredDev, wifiSsid;
    bool wifiConnected = false, wiredConnected = false;

    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = splitTerseLine(line);
        if (fields.size() < 4)
            continue;
        const QString device = fields[0], type = fields[1], state = fields[2], connection = fields[3];

        if (type == "wifi") {
            wifiDev = device;
            wifiConnected = (state == "connected");
            wifiSsid = wifiConnected ? connection : QString();
        } else if (type == "ethernet") {
            wiredDev = device;
            wiredConnected = (state == "connected");
        }
    }

    m_wifiDev = wifiDev;
    m_wiredDev = wiredDev;
    m_wifiConnected = wifiConnected;
    m_wifiSsid = wifiSsid;
    m_wiredConnected = wiredConnected;
}

void NetworkModel::parseDevShowIp(const QString &output, QString &ip, QString &mask, QString &gw)
{
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QString key = line.left(colon);
        const QString value = line.mid(colon + 1);

        if (ip.isEmpty() && key.startsWith("IP4.ADDRESS")) {
            const int slash = value.indexOf('/');
            if (slash >= 0) {
                ip = value.left(slash);
                mask = prefixToMask(value.mid(slash + 1).toInt());
            } else {
                ip = value;
            }
        } else if (key == "IP4.GATEWAY") {
            gw = value;
        }
    }
}

void NetworkModel::parseScanList(const QString &output)
{
    QMap<QString, QVariantMap> bySsid;

    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = splitTerseLine(line);
        if (fields.size() < 4)
            continue;
        const QString inUse = fields[0], ssid = fields[1], signalStr = fields[2], security = fields[3];

        if (ssid.isEmpty() || inUse == "*")
            continue;

        const int signal = signalStr.toInt();
        if (bySsid.contains(ssid) && bySsid[ssid].value("signal").toInt() >= signal)
            continue;

        QVariantMap m;
        m["ssid"] = ssid;
        m["signal"] = signal;
        m["secured"] = (security != "--" && !security.isEmpty());
        // `known` is set by parseKnownProfiles, which always runs immediately
        // after this in the scan chain (refreshScanList → refreshKnownProfiles).
        bySsid[ssid] = m;
    }

    QList<QVariantMap> list = bySsid.values();
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("signal").toInt() > b.value("signal").toInt();
    });

    QVariantList result;
    for (const QVariantMap &m : list)
        result.append(m);
    m_networks = result;
}

void NetworkModel::parseKnownProfiles(const QString &output)
{
    m_knownSsids.clear();
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QStringList fields = splitTerseLine(line);
        if (fields.size() < 2)
            continue;
        if (fields[1] == "802-11-wireless")
            m_knownSsids << fields[0];
    }

    for (int i = 0; i < m_networks.size(); ++i) {
        QVariantMap m = m_networks[i].toMap();
        m["known"] = m_knownSsids.contains(m.value("ssid").toString());
        m_networks[i] = m;
    }
}

// ── async nmcli runner ────────────────────────────────────────────────

void NetworkModel::runNmcli(const QStringList &args, int timeoutMs,
                             std::function<void(bool ok, int exitCode, const QString &out)> onDone)
{
    QProcess *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    auto outputBuf = std::make_shared<QString>();
    auto doneFlag = std::make_shared<bool>(false);

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, outputBuf]() {
        *outputBuf += QString::fromUtf8(proc->readAllStandardOutput());
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [proc, outputBuf, doneFlag, onDone](int exitCode, QProcess::ExitStatus status) {
            if (*doneFlag)
                return;
            *doneFlag = true;
            const bool ok = (status == QProcess::NormalExit && exitCode == 0);
            onDone(ok, exitCode, *outputBuf);
            proc->deleteLater();
        });

    connect(proc, &QProcess::errorOccurred, this,
        [proc, doneFlag, onDone](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart || *doneFlag)
                return;
            *doneFlag = true;
            onDone(false, -1, QStringLiteral("nmcli not found or failed to start"));
            proc->deleteLater();
        });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning)
                proc->kill();
        });
    }

    proc->start(QStringLiteral("nmcli"), args);
}

// ── mock mode ─────────────────────────────────────────────────────────

void NetworkModel::mockInit()
{
    m_nmAvailable = true;
    m_wifiEnabled = true;
    m_wiredConnected = false;
    m_knownSsids = {"PitLane-WiFi", "Marshals"};
    mockSetConnected("PitLane-WiFi");
}

// Shared "device is now on this network" mutation for the mock backend.
void NetworkModel::mockSetConnected(const QString &ssid)
{
    m_wifiConnected = true;
    m_wifiSsid = ssid;
    m_wifiIp = "192.168.1.42";
    m_wifiSubnet = "255.255.255.0";
    m_wifiGateway = "192.168.1.1";
    if (!m_knownSsids.contains(ssid))
        m_knownSsids.append(ssid);
    rebuildMockNetworks(ssid);
}

void NetworkModel::rebuildMockNetworks(const QString &activeSsid)
{
    QList<QVariantMap> list;
    for (const QVariantMap &entry : kMockPool) {
        const QString ssid = entry.value("ssid").toString();
        if (ssid == activeSsid)
            continue;
        QVariantMap m = entry;
        m["known"] = m_knownSsids.contains(ssid);
        list.append(m);
    }
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("signal").toInt() > b.value("signal").toInt();
    });

    QVariantList result;
    for (const QVariantMap &m : list)
        result.append(m);
    m_networks = result;
    emit networksChanged();
}

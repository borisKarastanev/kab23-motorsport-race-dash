#pragma once

#include <QObject>
#include <QString>

// Broker endpoint and the per-car credential for the cloud uplink.
//
// Deliberately NOT part of dashboard.conf. DashConfig is a layout and threshold
// store that the Settings UI rewrites on every stepper tap; this file holds a
// secret. Keeping them apart means the credential file can be 0600 and written
// through QSaveFile without the debounced, high-frequency write path
// DashConfig needs — and it means a future "reset dashboard layout" can never
// take the car's broker password with it.
//
// The password is issued once by the cloud's POST /cars/:id/mqtt-credentials
// and stored nowhere on the platform — not plaintext, not hashed. Losing it
// means rotating, never looking it up, so this file is the only copy.
//
// NON-NEGOTIABLE, and the reason several things here look paranoid:
// LogBufferModel installs a Qt message handler that captures *everything* and
// the Device Log settings page renders it on screen. A single stray qCInfo()
// carrying the password would put a live credential on a display in a car and
// into a log file on disk. So the password is never logged, never placed in an
// error string, never exposed to QML by a getter, and never written into
// --mock seed data.
class CloudConfig : public QObject {
    Q_OBJECT

    // Note what is absent: there is no `password` Q_PROPERTY. QML can *set* a
    // new one (see setPassword) and can ask *whether* one exists, but cannot
    // read it back — a bound property would be one accidental Text{} away from
    // rendering the secret.
    Q_PROPERTY(QString brokerHost READ brokerHost WRITE setBrokerHost NOTIFY changed)
    Q_PROPERTY(int     brokerPort READ brokerPort WRITE setBrokerPort NOTIFY changed)
    Q_PROPERTY(QString deviceId   READ deviceId   WRITE setDeviceId   NOTIFY changed)
    Q_PROPERTY(bool    useTls     READ useTls     WRITE setUseTls     NOTIFY changed)
    Q_PROPERTY(QString caFile     READ caFile     WRITE setCaFile     NOTIFY changed)
    Q_PROPERTY(bool    enabled    READ enabled    WRITE setEnabled    NOTIFY changed)
    Q_PROPERTY(bool    hasPassword READ hasPassword NOTIFY changed)

    // True once there is enough to attempt a connection at all. The settings
    // page shows `Unconfigured` on this rather than letting the uplink fail
    // silently — a missing credential must never look like a working uplink
    // that happens to be quiet.
    Q_PROPERTY(bool configured READ configured NOTIFY changed)

public:
    explicit CloudConfig(QObject *parent = nullptr);

    QString brokerHost() const { return m_brokerHost; }
    void    setBrokerHost(const QString &host);
    int     brokerPort() const { return m_brokerPort; }
    void    setBrokerPort(int port);
    QString deviceId()   const { return m_deviceId; }
    void    setDeviceId(const QString &id);
    bool    useTls()     const { return m_useTls; }
    void    setUseTls(bool on);
    QString caFile()     const { return m_caFile; }
    void    setCaFile(const QString &path);
    bool    enabled()    const { return m_enabled; }
    void    setEnabled(bool on);

    bool    hasPassword() const { return !m_password.isEmpty(); }
    bool    configured()  const;

    // C++-only. Intentionally not Q_INVOKABLE and not a property: the only
    // consumer is MosquittoCloudClient, on the same thread, immediately before
    // it hands the value to libmosquitto.
    QString password() const { return m_password; }

    // Q_INVOKABLE so the pairing UI can store what PasswordEntryModal
    // collected. Write-only by design — there is no getter reachable from QML.
    Q_INVOKABLE void setPassword(const QString &password);

    // Forgets the credential (pairing reset). Separate from setPassword("") so
    // the intent is explicit at the call site.
    Q_INVOKABLE void clearPassword();

signals:
    // One signal for everything. These values change only when a human edits
    // the pairing form, so there is nothing to gain from per-field notifies and
    // one less chance of wiring a notify to the wrong property.
    void changed();

private:
    void load();
    void persist();

    static QString configPath();

    QString m_brokerHost;
    int     m_brokerPort = 8883;
    QString m_deviceId;
    bool    m_useTls     = true;   // secure by default; see setUseTls
    QString m_caFile;              // empty => system CA bundle
    bool    m_enabled    = false;  // opt-in: an unpaired dash publishes nothing
    QString m_password;
};

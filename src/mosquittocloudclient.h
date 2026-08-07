#pragma once

#include "icloudclient.h"

#include <QString>

struct mosquitto;

class CloudConfig;

// libmosquitto-backed MQTT client.
//
// ## Threading
//
// libmosquitto runs its own network thread (mosquitto_loop_start), so
// on_connect / on_disconnect / on_publish all arrive on *that* thread, not the
// Qt main thread. Every callback here therefore does nothing but marshal back
// with QMetaObject::invokeMethod(..., Qt::QueuedConnection) before any Qt state
// is touched — the same discipline the repo already mandates for
// CanDataModel::onFrame.
//
// Getting this wrong would not crash loudly; it would corrupt QObject
// connection state under a rare race, on a device in a car.
//
// ## Reconnection
//
// libmosquitto's own exponential backoff (mosquitto_reconnect_delay_set) with
// mosquitto_loop_start's automatic retry. Deliberately not reimplemented on top:
// a hand-rolled QTimer reconnect racing the library's would produce two
// connection attempts in flight, and the failure mode of that on a marginal LTE
// link is a connect/disconnect loop that never settles.
//
// ## TLS
//
// mosquitto_tls_set is called for real, not stubbed. The broker is a public
// endpoint from the first connection, and a broker password sent over plaintext
// MQTT across LTE is a leaked credential. There is deliberately NO fallback to
// plaintext on a TLS error — a certificate failure must refuse the connection
// visibly, because silently downgrading is precisely the bug this guards.
class MosquittoCloudClient : public ICloudClient {
    Q_OBJECT

public:
    explicit MosquittoCloudClient(CloudConfig *config, QObject *parent = nullptr);
    ~MosquittoCloudClient() override;

    void connectToBroker() override;
    void disconnectFromBroker() override;
    bool isConnected() const override { return m_connected; }
    int  publish(const QString &topic, const QByteArray &payload, int qos) override;

private:
    // Called on the libmosquitto network thread. Each one immediately hops to
    // the main thread; nothing else happens in them.
    static void onConnectTrampoline(struct mosquitto *m, void *self, int rc);
    static void onDisconnectTrampoline(struct mosquitto *m, void *self, int rc);
    static void onPublishTrampoline(struct mosquitto *m, void *self, int mid);

    // Main-thread handlers invoked by the trampolines.
    Q_INVOKABLE void handleConnect(int rc);
    Q_INVOKABLE void handleDisconnect(int rc);
    Q_INVOKABLE void handlePublish(int mid);

    void teardown();

    CloudConfig      *m_config = nullptr;
    struct mosquitto *m_mosq   = nullptr;
    bool              m_connected  = false;
    bool              m_loopRunning = false;
};

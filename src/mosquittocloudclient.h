#pragma once

#include "icloudclient.h"

#include <QString>

#include <atomic>

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
// Queueing has a second consequence that is easy to miss: a callback raised on
// the network thread can still be sitting in the main thread's event queue after
// the handle it belongs to has been destroyed — which happens on every re-pair
// and every UNPAIR, not only at shutdown. Acting on a stale one would flip
// m_connected back to true against a null handle, and nothing could ever clear
// it again because no further callback can arrive from a destroyed client. Each
// teardown() therefore bumps m_generation, the trampolines stamp the value they
// saw, and the main-thread handlers drop anything from an earlier generation.
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

    // Main-thread handlers invoked by the trampolines. `generation` is the value
    // m_generation held when the callback fired; see the class note. Not
    // Q_INVOKABLE — the trampolines use invokeMethod's functor overload, which
    // needs no meta-object entry and is checked at compile time.
    void handleConnect(int rc, quint64 generation);
    void handleDisconnect(int rc, quint64 generation);
    void handlePublish(int mid, quint64 generation);

    // Whether a queued callback belongs to a handle that no longer exists.
    bool isStale(quint64 generation) const;

    // How mosquitto_loop_stop() is asked to end the network thread.
    //
    // ## KNOWN GAP — the Forced path can still cancel a TLS handshake
    //
    // `force=true` is pthread_cancel. Cancelling the network thread while it is
    // inside OpenSSL can leave process-global library state inconsistent — a
    // held lock, a leaked BIO — for the connect that follows. Destroying and
    // recreating the mosquitto handle does not clear that, because the damage is
    // not per-client.
    //
    // Graceful avoids it but is unbounded: mosquitto_loop_stop() without force
    // waits for the thread to return, and a thread blocked on a dead LTE link
    // does not return until the keepalive expires. That is a ~30 s frozen
    // dashboard, on the GUI thread, in a moving car.
    //
    // So the policy is a split rather than a fix, and it leaves the risk exactly
    // where it is worst:
    //
    //   - connected      -> Graceful. A DISCONNECT has just gone out over a live
    //                       socket, so the thread returns promptly. This is the
    //                       re-pair/UNPAIR path in practice, and it is no longer
    //                       a cancel.
    //   - not connected  -> Forced. Mid-handshake or mid-backoff against an
    //                       unreachable host is precisely when cancelling is
    //                       riskiest AND when Graceful would hang longest. The
    //                       hang is the worse failure, so it is still cancelled.
    //   - destructor     -> Forced, unconditionally. At process exit nothing
    //                       follows the cancel, so the hazard does not apply.
    //
    // A real fix is asynchronous teardown: send the DISCONNECT, return to the
    // event loop, and destroy the handle from the on_disconnect callback with a
    // short QTimer as the bound. That is graceful in the common case and never
    // blocks, but it needs connectToBroker() to tolerate a handle that is still
    // retiring while a new one is built, so it was left out of the review fix
    // that introduced this split.
    enum class StopMode {
        Graceful, // wait for the thread to return — only safe once a DISCONNECT
                  // has gone out over a live socket
        Forced    // pthread_cancel it; bounded, but can cut a TLS handshake
    };
    void teardown(StopMode mode);

    // Sends a DISCONNECT, tears the handle down, and emits disconnected() if we
    // were connected. Everything that ends a live link goes through here.
    void shutdownLink();

    // Applies the TLS settings (or the plaintext warning) to a freshly created
    // handle. False means the connection must be refused — it has already
    // emitted errorOccurred(); see the TLS note above for why there is no
    // fallback.
    bool configureTls();

    CloudConfig      *m_config = nullptr;
    struct mosquitto *m_mosq   = nullptr;
    bool              m_connected  = false;
    bool              m_loopRunning = false;

    // Read on the network thread, written on the main thread — hence atomic.
    std::atomic<quint64> m_generation{ 0 };
};

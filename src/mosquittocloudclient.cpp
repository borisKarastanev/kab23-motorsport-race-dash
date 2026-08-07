#include "mosquittocloudclient.h"
#include "cloudconfig.h"

#include <QLoggingCategory>
#include <QMetaObject>

#include <mosquitto.h>

Q_LOGGING_CATEGORY(lcCloud, "dash.uplink.mqtt")

namespace {

// libmosquitto requires exactly one lib_init/lib_cleanup pair per process, not
// per client. A static guard keeps that true even if a second client is ever
// constructed (tests, a future second broker) without making the caller
// responsible for remembering.
struct MosquittoLibrary {
    MosquittoLibrary()  { mosquitto_lib_init(); }
    ~MosquittoLibrary() { mosquitto_lib_cleanup(); }
};

void ensureLibraryInitialised()
{
    static MosquittoLibrary library;
    Q_UNUSED(library)
}

constexpr int kKeepAliveSeconds = 30;
constexpr int kReconnectDelayMin = 1;
constexpr int kReconnectDelayMax = 60;

} // namespace

MosquittoCloudClient::MosquittoCloudClient(CloudConfig *config, QObject *parent)
    : ICloudClient(parent)
    , m_config(config)
{
    // Nothing but member assignment. Construction happens during startup and
    // the boot path must not open a socket, read the SD card, or block before
    // the first frame is on screen — see main.cpp.
}

MosquittoCloudClient::~MosquittoCloudClient()
{
    // No shutdownLink() here: emitting disconnected() out of a destructor would
    // re-enter UplinkModel while this object is half-destroyed. Forced, because
    // at process exit latency is the only thing that matters.
    teardown(StopMode::Forced);
}

void MosquittoCloudClient::teardown(StopMode mode)
{
    // Bumped unconditionally, before anything else: from here on, any callback
    // already queued to the main thread belongs to a handle that is going away
    // and must be ignored. See the class note.
    ++m_generation;

    if (!m_mosq)
        return;

    if (m_loopRunning) {
        // force=true is pthread_cancel on the network thread. That is the right
        // trade at shutdown — without it this blocks until the thread finishes
        // whatever it is doing, which on a dead LTE link is the full keepalive
        // timeout, i.e. a 30 s hang in a car — but it is the wrong one on the
        // re-pair path, where cancelling mid-OpenSSL-handshake can leave the
        // library's own state inconsistent for the connect that follows
        // immediately after. shutdownLink() asks for Graceful once it has sent a
        // DISCONNECT over a live socket, where the thread returns promptly.
        mosquitto_loop_stop(m_mosq, mode == StopMode::Forced);
        m_loopRunning = false;
    }
    mosquitto_destroy(m_mosq);
    m_mosq = nullptr;
    m_connected = false;
}

void MosquittoCloudClient::shutdownLink()
{
    const bool wasConnected = m_connected;

    if (m_mosq)
        mosquitto_disconnect(m_mosq);

    // Graceful only when there was a live socket to send the DISCONNECT on;
    // otherwise the thread may be blocked in a connect or a handshake and a
    // non-forced stop would freeze the GUI thread for the keepalive interval.
    teardown(wasConnected ? StopMode::Graceful : StopMode::Forced);

    // The whole reason this function exists. teardown() clears m_connected
    // directly, and libmosquitto's own on_disconnect cannot be relied on to
    // cover for it: loop_stop may end the network thread before the callback
    // fires, and even when it does fire, the queued hop to the main thread lands
    // after m_connected is already false — so handleDisconnect() sees
    // wasConnected == false and stays silent too. That left
    // UplinkModel::onClientDisconnected() — the only place the drain state is
    // reset — never running on an app-initiated disconnect, which stranded
    // m_draining true and sent every subsequent frame to the spool forever.
    if (wasConnected)
        emit disconnected();
}

void MosquittoCloudClient::connectToBroker()
{
    if (!m_config || !m_config->configured()) {
        emit errorOccurred(tr("Cloud uplink is not paired"));
        return;
    }

    ensureLibraryInitialised();
    // Not a bare teardown(): re-pairing while online is an app-initiated
    // disconnect like any other, and UplinkModel has drain state that only
    // disconnected() clears.
    shutdownLink();

    const QByteArray clientId = m_config->deviceId().toUtf8();

    // clean_session=true. The broker holds no subscriptions for this client (a
    // car may not subscribe at all under the ACL) and no session state worth
    // resuming — durability lives in UplinkSpool on this side, deliberately, so
    // that a broker-side queue cannot become a second source of truth.
    m_mosq = mosquitto_new(clientId.constData(), true, this);
    if (!m_mosq) {
        emit errorOccurred(tr("Could not create MQTT client"));
        return;
    }

    mosquitto_connect_callback_set(m_mosq, &MosquittoCloudClient::onConnectTrampoline);
    mosquitto_disconnect_callback_set(m_mosq, &MosquittoCloudClient::onDisconnectTrampoline);
    mosquitto_publish_callback_set(m_mosq, &MosquittoCloudClient::onPublishTrampoline);

    const QByteArray username = m_config->deviceId().toUtf8();
    const QByteArray password = m_config->password().toUtf8();
    mosquitto_username_pw_set(m_mosq, username.constData(), password.constData());

    if (m_config->useTls()) {
        const QString caFile = m_config->caFile();
        const QByteArray ca = caFile.toUtf8();

        // Null CA path => use the system trust store, which is what a real
        // Let's Encrypt certificate needs. A path is only for a private CA
        // (the local rehearsal stack).
        const int rc = mosquitto_tls_set(
            m_mosq,
            caFile.isEmpty() ? nullptr : ca.constData(),
            caFile.isEmpty() ? "/etc/ssl/certs" : nullptr,
            nullptr, nullptr, nullptr);

        if (rc != MOSQ_ERR_SUCCESS) {
            // Refuse, do not fall back. A downgrade to plaintext here would put
            // the broker password on the air over LTE.
            emit errorOccurred(tr("TLS setup failed: %1")
                                   .arg(QString::fromUtf8(mosquitto_strerror(rc))));
            teardown(StopMode::Forced);
            return;
        }

        // Verify the peer certificate and that its name matches the host. This
        // is libmosquitto's default, set explicitly because it is the entire
        // security property being relied on and a future edit should have to
        // delete a visible line to break it.
        mosquitto_tls_opts_set(m_mosq, 1 /* SSL_VERIFY_PEER */, nullptr, nullptr);
        mosquitto_tls_insecure_set(m_mosq, false);
    } else {
        // Loud, and on every connect rather than once: this is a development
        // affordance for a local broker, and it must never be quietly true on a
        // car that is publishing over the internet.
        qCWarning(lcCloud) << "TLS is DISABLED — credential will cross the network "
                              "in the clear. Development only.";
    }

    mosquitto_reconnect_delay_set(m_mosq, kReconnectDelayMin, kReconnectDelayMax,
                                  true /* exponential backoff */);

    const QByteArray host = m_config->brokerHost().toUtf8();
    const int rc = mosquitto_connect_async(m_mosq, host.constData(),
                                           m_config->brokerPort(), kKeepAliveSeconds);
    if (rc != MOSQ_ERR_SUCCESS) {
        emit errorOccurred(tr("Connect failed: %1")
                               .arg(QString::fromUtf8(mosquitto_strerror(rc))));
        teardown(StopMode::Forced);
        return;
    }

    if (mosquitto_loop_start(m_mosq) != MOSQ_ERR_SUCCESS) {
        emit errorOccurred(tr("Could not start MQTT network thread"));
        teardown(StopMode::Forced);
        return;
    }
    m_loopRunning = true;

    // Host and port only. Never the username (it is the deviceId, harmless) and
    // never anything derived from the password.
    qCInfo(lcCloud) << "connecting to" << m_config->brokerHost()
                    << "port" << m_config->brokerPort()
                    << "tls" << m_config->useTls();
}

void MosquittoCloudClient::disconnectFromBroker()
{
    shutdownLink();
}

int MosquittoCloudClient::publish(const QString &topic, const QByteArray &payload, int qos)
{
    if (!m_mosq || !m_connected)
        return -1;

    int mid = 0;
    const QByteArray topicBytes = topic.toUtf8();
    const int rc = mosquitto_publish(m_mosq, &mid, topicBytes.constData(),
                                     payload.size(), payload.constData(), qos,
                                     false /* retain */);
    // Retain is false everywhere, deliberately: a retained `start` event would
    // re-open a session on the cloud every time ingest reconnects.

    if (rc != MOSQ_ERR_SUCCESS) {
        qCWarning(lcCloud) << "publish to" << topic << "failed:"
                           << mosquitto_strerror(rc);
        return -1;
    }
    return mid;
}

// --- libmosquitto network-thread callbacks -----------------------------------
//
// Each of these is running on libmosquitto's thread. They touch nothing but the
// queued invocation, because everything else here is main-thread state.

// The functor overload of invokeMethod, not the "methodName" one: the generation
// stamp is an extra argument on each handler, and a string-matched invocation
// that failed to resolve would fail at *runtime*, as a warning nobody reads,
// leaving the uplink permanently unable to learn it had connected. This form is
// checked by the compiler. The receiver context is still the client, so Qt drops
// any invocation left pending when it is destroyed.

void MosquittoCloudClient::onConnectTrampoline(struct mosquitto *, void *self, int rc)
{
    auto *client = static_cast<MosquittoCloudClient *>(self);
    const quint64 generation = client->m_generation.load();
    QMetaObject::invokeMethod(client, [client, rc, generation] {
        client->handleConnect(rc, generation);
    }, Qt::QueuedConnection);
}

void MosquittoCloudClient::onDisconnectTrampoline(struct mosquitto *, void *self, int rc)
{
    auto *client = static_cast<MosquittoCloudClient *>(self);
    const quint64 generation = client->m_generation.load();
    QMetaObject::invokeMethod(client, [client, rc, generation] {
        client->handleDisconnect(rc, generation);
    }, Qt::QueuedConnection);
}

void MosquittoCloudClient::onPublishTrampoline(struct mosquitto *, void *self, int mid)
{
    auto *client = static_cast<MosquittoCloudClient *>(self);
    const quint64 generation = client->m_generation.load();
    QMetaObject::invokeMethod(client, [client, mid, generation] {
        client->handlePublish(mid, generation);
    }, Qt::QueuedConnection);
}

// --- main thread -------------------------------------------------------------

// A queued callback is stale when the handle that raised it has since been
// destroyed by teardown(). Such a callback must be dropped rather than acted on:
// handleConnect() in particular would otherwise set m_connected against a null
// m_mosq, leaving a client that reports itself online, cannot publish, and can
// never be disconnected again — no further callback can arrive from a handle
// that no longer exists.
bool MosquittoCloudClient::isStale(quint64 generation) const
{
    return generation != m_generation.load() || !m_mosq;
}

void MosquittoCloudClient::handleConnect(int rc, quint64 generation)
{
    if (isStale(generation))
        return;

    if (rc != 0) {
        m_connected = false;
        // rc 4/5 are bad-credentials / not-authorised. Say which, because "the
        // uplink is not working" with no reason is the state that wastes a
        // track day — but never echo the credential itself.
        const QString reason = (rc == 4 || rc == 5)
                                   ? tr("Broker rejected the credential")
                                   : QString::fromUtf8(mosquitto_connack_string(rc));
        qCWarning(lcCloud) << "connect refused:" << reason;
        emit errorOccurred(reason);
        return;
    }

    m_connected = true;
    qCInfo(lcCloud) << "connected";
    emit connected();
}

void MosquittoCloudClient::handleDisconnect(int rc, quint64 generation)
{
    if (isStale(generation))
        return;

    const bool wasConnected = m_connected;
    m_connected = false;

    // rc == 0 means we asked for it; anything else is the link dropping, which
    // on a track is normal and not worth an error banner.
    if (rc != 0)
        qCInfo(lcCloud) << "disconnected (link lost) — libmosquitto will retry";

    if (wasConnected)
        emit disconnected();
}

void MosquittoCloudClient::handlePublish(int mid, quint64 generation)
{
    if (isStale(generation))
        return;

    // mid values are per-handle. Releasing spool rows against a PUBACK from a
    // client that has since been destroyed could acknowledge a batch that the
    // current one never sent.
    emit published(mid);
}

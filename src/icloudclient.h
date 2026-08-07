#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Transport seam for the cloud uplink, following the repo's existing
// interface/mock split (ICanProvider -> Mock/Real, IRaceProvider -> Mock/Real).
//
// The split exists so UplinkModel — which owns all the interesting logic:
// framing, session lifecycle, sid/seq, the online/offline decision, drain
// ordering — can be tested without a broker, a network, or a background thread.
// MockCloudClient is for unit tests only; `--mock` runs use the real client
// pointed at whatever CloudConfig says, so bench-testing exercises the real
// libmosquitto path.
class ICloudClient : public QObject {
    Q_OBJECT

public:
    explicit ICloudClient(QObject *parent = nullptr) : QObject(parent) {}
    ~ICloudClient() override = default;

    // Asynchronous. `connected()` or `errorOccurred()` follows.
    virtual void connectToBroker() = 0;

    // MUST emit `disconnected()` if the link was up, including when the
    // disconnect is app-initiated and the transport's own callback never fires
    // or fires too late to see it. This is a hard requirement, not a courtesy:
    // UplinkModel::onClientDisconnected() is the only place its drain
    // bookkeeping is reset, so a silent drop strands it mid-batch and every
    // subsequent frame goes to the spool for the rest of the run while the UI
    // still reports ONLINE. MosquittoCloudClient::shutdownLink() exists for
    // exactly this, and MockCloudClient::dropLinkSilently() models the failure.
    virtual void disconnectFromBroker() = 0;

    virtual bool isConnected() const = 0;

    // Returns the message id, or -1 if the publish could not be handed to the
    // transport at all. For QoS 1 the id is what `published(mid)` reports on
    // PUBACK — that is the signal UplinkSpool waits for before deleting rows,
    // so it must be the broker's acknowledgement and not a local echo.
    virtual int publish(const QString &topic, const QByteArray &payload, int qos) = 0;

signals:
    void connected();
    void disconnected();

    // QoS 1 PUBACK for `mid`.
    void published(int mid);

    // Human-readable and safe to display: implementations must never put the
    // credential in here. It reaches the Settings page and the Device Log.
    void errorOccurred(const QString &message);
};

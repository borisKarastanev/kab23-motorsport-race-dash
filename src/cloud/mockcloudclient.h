#pragma once

#include "src/cloud/icloudclient.h"

#include <QList>
#include <QPair>

// In-memory ICloudClient for unit tests.
//
// NOT what `--mock` mode uses: mock runs drive the real MosquittoCloudClient
// against whatever CloudConfig points at (a local broker, or the deployed one),
// so bench-testing exercises the real libmosquitto path including TLS. This
// exists so tst_uplinkmodel can assert on framing, sequencing and the
// online/offline decision without a broker or a background thread.
//
// PUBACK is manual (`acknowledge`) precisely because the interesting UplinkModel
// behaviour is what happens in the window between publishing a backfill batch
// and it being acknowledged.
class MockCloudClient : public ICloudClient {
    Q_OBJECT

public:
    struct Message {
        QString    topic;
        QByteArray payload;
        int        qos = 0;
        int        mid = 0;
    };

    using ICloudClient::ICloudClient;

    void connectToBroker() override
    {
        if (m_connectShouldFail) {
            emit errorOccurred(QStringLiteral("mock: refused"));
            return;
        }
        m_connected = true;
        emit connected();
    }

    void disconnectFromBroker() override
    {
        if (!m_connected)
            return;
        m_connected = false;
        emit disconnected();
    }

    bool isConnected() const override { return m_connected; }

    int publish(const QString &topic, const QByteArray &payload, int qos) override
    {
        if (!m_connected)
            return -1;
        const int mid = ++m_nextMid;
        m_sent.append(Message{ topic, payload, qos, mid });
        // An id is returned whatever the QoS, as the real client does. What
        // differs is that nothing here ever emits published() on its own: a
        // PUBACK arrives only when a test asks for one, so a test that waits for
        // a QoS-0 acknowledgement hangs exactly as production would.
        return mid;
    }

    // Test controls ----------------------------------------------------------

    void acknowledge(int mid) { emit published(mid); }
    void acknowledgeLast()    { if (!m_sent.isEmpty()) emit published(m_sent.last().mid); }

    void dropLink()
    {
        m_connected = false;
        emit disconnected();
    }

    // A link that goes down without disconnected() reaching UplinkModel.
    //
    // Not a hypothetical: MosquittoCloudClient::disconnectFromBroker() used to
    // do exactly this — teardown() cleared its connected flag directly, and the
    // libmosquitto callback that would have reported it either never fired
    // (loop_stop kills the network thread) or arrived after the flag was already
    // false and stayed silent. onClientDisconnected() is the only place
    // UplinkModel resets its drain bookkeeping, so this models the one input
    // that could strand it.
    void dropLinkSilently() { m_connected = false; }

    void setConnectShouldFail(bool fail) { m_connectShouldFail = fail; }

    // Raises the error the way the real client does — through the signal — so a
    // test exercises UplinkModel's wiring rather than calling its private slot.
    void raiseError(const QString &message) { emit errorOccurred(message); }

    const QList<Message> &sent() const { return m_sent; }
    void clearSent() { m_sent.clear(); }

    QList<Message> sentOn(const QString &topicSuffix) const
    {
        QList<Message> out;
        for (const Message &m : m_sent)
            if (m.topic.endsWith(topicSuffix))
                out.append(m);
        return out;
    }

private:
    bool           m_connected = false;
    bool           m_connectShouldFail = false;
    int            m_nextMid = 0;
    QList<Message> m_sent;
};

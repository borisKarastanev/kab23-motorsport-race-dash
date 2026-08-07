#include "uplinkmodel.h"

#include "candatamodel.h"
#include "cloudconfig.h"
#include "icloudclient.h"
#include "raceboxmodel.h"
#include "trackmodel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QUuid>
#include <algorithm>

Q_LOGGING_CATEGORY(lcUplink, "dash.uplink")

namespace {

// Wire contract version. Shared verbatim with the cloud's ingest DTOs; bump
// only alongside that side.
constexpr int kWireVersion = 1;

constexpr int kQosTelemetry = 0; // fire-and-forget: a lost live frame is stale anyway
constexpr int kQosSession   = 1; // lifecycle must arrive
constexpr int kQosBackfill  = 1; // the PUBACK is what releases spool rows

} // namespace

UplinkModel::UplinkModel(CanDataModel *can,
                         RaceBoxModel *raceBox,
                         TrackModel   *track,
                         CloudConfig  *config,
                         ICloudClient *client,
                         QObject      *parent)
    : QObject(parent)
    , m_can(can)
    , m_raceBox(raceBox)
    , m_track(track)
    , m_config(config)
    , m_client(client)
{
    // The single monotonic source for the whole process lifetime. Started here
    // and never touched again.
    m_mono.start();

    m_sampleTimer.setInterval(kSampleIntervalMs);
    m_sampleTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sampleTimer, &QTimer::timeout, this, &UplinkModel::onSampleTick);

    m_queuedNotifyTimer.setInterval(kQueuedNotifyMs);
    m_queuedNotifyTimer.setSingleShot(true);
    connect(&m_queuedNotifyTimer, &QTimer::timeout, this, [this] {
        emit queuedFramesChanged();
    });

    if (m_client) {
        connect(m_client, &ICloudClient::connected,     this, &UplinkModel::onClientConnected);
        connect(m_client, &ICloudClient::disconnected,  this, &UplinkModel::onClientDisconnected);
        connect(m_client, &ICloudClient::published,     this, &UplinkModel::onClientPublished);
        connect(m_client, &ICloudClient::errorOccurred, this, &UplinkModel::onClientError);
    }

    // No spool open, no socket, no timer started. start() does all of that.
    setState(enabled() ? (m_config && m_config->configured() ? Connecting : Unconfigured)
                       : Disabled);
}

bool UplinkModel::enabled() const
{
    return m_config && m_config->enabled();
}

QString UplinkModel::deviceId() const
{
    return m_config ? m_config->deviceId() : QString();
}

QString UplinkModel::brokerSummary() const
{
    if (!m_config || m_config->brokerHost().isEmpty())
        return tr("Not set");
    return QStringLiteral("%1:%2%3")
        .arg(m_config->brokerHost())
        .arg(m_config->brokerPort())
        .arg(m_config->useTls() ? QString() : tr("  (no TLS)"));
}

QString UplinkModel::stateText() const
{
    switch (m_state) {
    case Disabled:     return tr("Off");
    case Unconfigured: return tr("Not paired");
    case Connecting:   return tr("Connecting…");
    case Online:       return tr("Online");
    case Offline:      return tr("Offline — buffering");
    }
    return QString();
}

void UplinkModel::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void UplinkModel::setLastError(const QString &message)
{
    // Whatever reaches here is rendered in Settings and captured by
    // LogBufferModel into the on-screen Device Log. Every producer of this
    // string is responsible for it carrying no credential; see
    // MosquittoCloudClient::handleConnect, which reports "Broker rejected the
    // credential" rather than anything derived from the secret itself.
    m_lastError = message;
    emit stateChanged();
}

void UplinkModel::start()
{
    if (m_started)
        return;
    m_started = true;

    if (!enabled()) {
        setState(Disabled);
        return;
    }

    // First touch of the SD card, deliberately after the first frame is on
    // screen rather than during construction.
    m_spool.open();
    refreshQueuedFrames();

    applyConfiguration();
}

void UplinkModel::applyConfiguration()
{
    if (!enabled()) {
        m_sampleTimer.stop();
        if (m_client)
            m_client->disconnectFromBroker();
        setState(Disabled);
        return;
    }

    if (!m_config->configured()) {
        m_sampleTimer.stop();
        setState(Unconfigured);
        return;
    }

    if (!m_started)
        return; // start() will get here once the first frame has been presented

    setLastError(QString());
    setState(Connecting);
    m_sampleTimer.start();
    if (m_client)
        m_client->connectToBroker();
}

// --- session lifecycle -------------------------------------------------------

void UplinkModel::onLapTimerStateChanged()
{
    if (!m_raceBox)
        return;

    const bool running = m_raceBox->lapTimerState() == RaceBoxModel::Running;

    // Only the transition into Running opens a session. Reusing the dash's own
    // lap-timer lifecycle rather than inventing a second notion of "a run" is
    // what keeps the cloud's session list agreeing with what the driver saw.
    if (running && !m_sessionActive)
        beginSession();
}

void UplinkModel::onSessionSaved()
{
    if (m_sessionActive)
        endSession();
}

void UplinkModel::onLapCompleted(qint64 ms, const QVariantList &path)
{
    Q_UNUSED(path) // the cloud rebuilds the racing line from the GPS trace
    if (m_sessionActive)
        m_lapTimesMs.append(ms);
}

void UplinkModel::beginSession()
{
    m_sid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_seq = 0;
    m_lapTimesMs.clear();
    m_topSpeedKmh = 0;
    m_sessionActive = true;
    emit sessionActiveChanged();

    qCInfo(lcUplink) << "session start" << m_sid;
    dispatch(sessionTopic(), buildSessionEvent(QStringLiteral("start")), 0, kQosSession);
}

void UplinkModel::endSession()
{
    qCInfo(lcUplink) << "session stop" << m_sid;
    dispatch(sessionTopic(), buildSessionEvent(QStringLiteral("stop")), 0, kQosSession);

    m_sessionActive = false;
    emit sessionActiveChanged();
    // m_sid deliberately survives: a backlog still in the spool belongs to it,
    // and the drain re-reads the sid from each row anyway.
}

// --- sampling ----------------------------------------------------------------

void UplinkModel::onSampleTick()
{
    if (!m_sessionActive || !enabled())
        return;

    if (m_can)
        m_topSpeedKmh = std::max(m_topSpeedKmh, m_can->speed());

    dispatch(telemetryTopic(), buildFrame(), m_seq, kQosTelemetry);
    ++m_seq;
}

QByteArray UplinkModel::buildFrame() const
{
    QJsonObject frame;
    frame["v"]    = kWireVersion;
    frame["sid"]  = m_sid;
    frame["seq"]  = static_cast<qint64>(m_seq);
    frame["ts"]   = QDateTime::currentMSecsSinceEpoch();
    frame["mono"] = m_mono.elapsed();

    if (m_can) {
        frame["rpm"]     = m_can->rpm();
        frame["coolant"] = m_can->coolantTemp();
        frame["oil"]     = m_can->oilTemp();
        frame["speed"]   = m_can->speed();
    }

    if (m_raceBox) {
        // Latest-value decimation, not interpolation: RaceBox delivers at 25 Hz
        // and every other consumer of these models already reads the newest
        // value, so this stays consistent with what the driver sees on screen.
        frame["lat"]   = m_raceBox->lastLat();
        frame["lon"]   = m_raceBox->lastLon();
        frame["gx"]    = m_raceBox->gForceX();
        frame["gy"]    = m_raceBox->gForceY();
        frame["gz"]    = m_raceBox->gForceZ();
        frame["lap"]   = m_raceBox->lapNumber();
        frame["lapMs"] = m_raceBox->currentLapMs();
        frame["fix"]   = m_raceBox->hasFix() ? 3 : 0;
        frame["sats"]  = m_raceBox->satellites();
    }

    frame["ext"] = QJsonObject();

    return QJsonDocument(frame).toJson(QJsonDocument::Compact);
}

QByteArray UplinkModel::buildSessionEvent(const QString &event) const
{
    QJsonObject payload;
    payload["v"]     = kWireVersion;
    payload["sid"]   = m_sid;
    payload["event"] = event;
    payload["at"]    = QDateTime::currentMSecsSinceEpoch();
    payload["mono"]  = m_mono.elapsed();

    if (event == QLatin1String("start")) {
        if (m_track) {
            payload["track"]     = m_track->activeTrackId();
            payload["trackName"] = m_track->activeTrackName();
        }
    } else {
        // Mirrors what SessionModel::saveCurrentSession() computes, from the
        // same RaceBoxModel::lapCompleted signal, so the cloud's summary and the
        // car's agree rather than being two independent measurements.
        QJsonArray laps;
        for (qint64 lap : m_lapTimesMs)
            laps.append(lap);
        payload["laps"] = laps;
        payload["topSpeedKmh"] = m_topSpeedKmh;
    }

    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

// --- dispatch: live or spool -------------------------------------------------

void UplinkModel::dispatch(const QString &topic, const QByteArray &payload,
                           qint64 seq, int qos)
{
    // While draining, everything goes to the spool even though the link is up.
    // That is the point: the cloud must see the backlog before the present, or
    // a viewer's map would jump forward and then rewind.
    const bool live = m_client && m_client->isConnected() && !m_draining;

    if (!live) {
        spool(topic, payload, seq);
        return;
    }

    if (m_client->publish(topic, payload, qos) < 0)
        spool(topic, payload, seq);
}

void UplinkModel::spool(const QString &topic, const QByteArray &payload, qint64 seq)
{
    SpooledFrame row;
    row.topic   = topic;
    row.sid     = m_sid;
    row.seq     = seq;
    row.payload = payload;
    m_spool.append(row);
    refreshQueuedFrames();
}

// --- drain -------------------------------------------------------------------

void UplinkModel::onClientConnected()
{
    setState(Online);
    setLastError(QString());

    if (m_spool.count() > 0) {
        m_draining = true;
        drainNext();
    }
}

void UplinkModel::onClientDisconnected()
{
    // Not an error state: a dropout at a track is normal, and libmosquitto is
    // already retrying with backoff. Frames go to the spool meanwhile.
    m_draining = false;
    m_inFlightMid = -1;
    m_inFlightLastId = -1;
    if (enabled() && m_config && m_config->configured())
        setState(Offline);
}

void UplinkModel::onClientError(const QString &message)
{
    setLastError(message);
    if (enabled())
        setState(m_config && m_config->configured() ? Offline : Unconfigured);
}

void UplinkModel::drainNext()
{
    if (!m_draining || !m_client || !m_client->isConnected())
        return;

    const QVector<SpooledFrame> batch = m_spool.peekRun();
    if (batch.isEmpty()) {
        // Caught up. Live publishing resumes on the next sample tick.
        m_draining = false;
        m_inFlightMid = -1;
        m_inFlightLastId = -1;
        refreshQueuedFrames();
        qCInfo(lcUplink) << "backlog drained";
        return;
    }

    const bool isTelemetry = batch.first().topic == telemetryTopic();

    int mid = -1;
    if (isTelemetry) {
        // Telemetry replays on the dedicated backfill topic, batched. The cloud
        // deliberately does NOT publish backfill to its live bus — a drained
        // backlog arriving on the live channel would rewind every viewer's map.
        QJsonArray frames;
        for (const SpooledFrame &row : batch)
            frames.append(QJsonDocument::fromJson(row.payload).object());

        QJsonObject envelope;
        envelope["v"]      = kWireVersion;
        envelope["sid"]    = batch.first().sid;
        envelope["frames"] = frames;

        mid = m_client->publish(backfillTopic(),
                                QJsonDocument(envelope).toJson(QJsonDocument::Compact),
                                kQosBackfill);
    } else {
        // Session events replay one at a time on their own topic, unbatched —
        // there is no envelope for them in the wire contract, and there are
        // only ever two per session.
        mid = m_client->publish(batch.first().topic, batch.first().payload, kQosSession);
    }

    if (mid < 0) {
        m_draining = false;
        return;
    }

    m_inFlightMid    = mid;
    // Rows are released only when this id is acknowledged — never here. A power
    // cut between publish and PUBACK therefore re-sends, which the cloud's
    // (session, seq, time) dedup index makes free.
    m_inFlightLastId = isTelemetry ? batch.last().id : batch.first().id;
}

void UplinkModel::onClientPublished(int mid)
{
    if (!m_draining || mid != m_inFlightMid)
        return;

    m_spool.releaseThrough(m_inFlightLastId);
    refreshQueuedFrames();
    drainNext();
}

void UplinkModel::refreshQueuedFrames()
{
    const int count = m_spool.count();
    if (count == m_queuedFrames)
        return;
    m_queuedFrames = count;

    // Coalesced: a drain moves this 200 at a time and the settings page does not
    // need to see every step.
    if (!m_queuedNotifyTimer.isActive())
        m_queuedNotifyTimer.start();
}

// --- topics ------------------------------------------------------------------
//
// Must match libs/common/src/constants/mqtt-topics.ts on the cloud side, and the
// per-car ACL: a car may publish only under cars/<its-deviceId>/.

QString UplinkModel::telemetryTopic() const
{
    return QStringLiteral("cars/%1/telemetry").arg(deviceId());
}

QString UplinkModel::sessionTopic() const
{
    return QStringLiteral("cars/%1/session").arg(deviceId());
}

QString UplinkModel::backfillTopic() const
{
    return QStringLiteral("cars/%1/backfill").arg(deviceId());
}

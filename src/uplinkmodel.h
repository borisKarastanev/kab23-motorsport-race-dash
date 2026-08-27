#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "uplinkspool.h"
#include "raceboxmodel.h"

class CanDataModel;
class TrackModel;
class CloudConfig;
class ICloudClient;

// The cloud uplink: samples the live models at 10 Hz, frames them to the wire
// contract shared with the telemetry platform, and gets them to the broker —
// live when there is a link, out of the spool when there is not.
//
// ## What owns a "session"
//
// The dash does, and it reuses the notion it already has rather than inventing
// a second one. `start` goes out when the lap timer enters Running; `stop` on
// SessionModel::sessionSaved. If those two ever disagreed with what the driver
// sees on screen, the cloud's session list would disagree with the car's.
//
// ## sid, seq and mono
//
// - `sid` is a UUID minted per session. It is the cloud's idempotency key: a
//   replayed backfill batch must land in the *same* session row, not open a
//   second one.
// - `seq` is monotonic within a sid and backs the cloud's (session, seq, time)
//   dedup index. That index is what makes re-sending an unacknowledged batch
//   free — so this class must never dedupe on its own. When in doubt, re-send.
// - `mono` is milliseconds from a single free-running QElapsedTimer started once
//   at construction and never restarted, including across session boundaries. A
//   Pi 4 has no RTC, so wall-clock `ts` is unreliable until NTP syncs over LTE;
//   the cloud anchors a session to its own clock at `start` and advances it by
//   `mono` deltas. Restarting this timer would silently compress every sample in
//   the session onto the wrong timestamps.
class UplinkModel : public QObject {
    Q_OBJECT

public:
    enum State {
        Disabled,      // the driver has not switched the uplink on
        Unconfigured,  // on, but no broker/deviceId/credential
        Connecting,
        Online,
        Offline        // configured and trying; frames are going to the spool
    };
    Q_ENUM(State)

private:
    // No `enabled` property: QML reads the fact off CloudConfig, which owns it.
    // Two bound names for one value is one of them going stale.
    Q_PROPERTY(State   state         READ state         NOTIFY stateChanged)
    Q_PROPERTY(QString stateText     READ stateText     NOTIFY stateChanged)
    Q_PROPERTY(QString deviceId      READ deviceId      NOTIFY stateChanged)
    Q_PROPERTY(QString brokerSummary READ brokerSummary NOTIFY stateChanged)
    Q_PROPERTY(int     queuedFrames  READ queuedFrames  NOTIFY queuedFramesChanged)
    Q_PROPERTY(QString lastError     READ lastError     NOTIFY stateChanged)
    Q_PROPERTY(bool    sessionActive READ sessionActive NOTIFY sessionActiveChanged)

public:
    // `client` is owned by the caller (main.cpp) so tests can substitute
    // MockCloudClient. Nothing here does I/O — see start().
    UplinkModel(CanDataModel *can,
                RaceBoxModel *raceBox,
                TrackModel   *track,
                CloudConfig  *config,
                ICloudClient *client,
                QObject      *parent = nullptr);

    bool    enabled()       const;
    State   state()         const { return m_state; }
    QString stateText()     const;
    QString deviceId()      const;
    QString brokerSummary() const;
    int     queuedFrames()  const { return m_queuedFrames; }
    QString lastError()     const { return m_lastError; }
    bool    sessionActive() const { return m_sessionActive; }

    // Opens the spool and starts connecting. Called from main.cpp off the first
    // frameSwapped, never during construction: the boot path is tuned hard and
    // nothing on the uplink may run before the first frame is on screen.
    void start();

    // Closes an open session and drops the link. Wired to
    // QCoreApplication::aboutToQuit in main.cpp — process exit is the last
    // chance to record that a session ended, and switching the ignition off is
    // how most of them do end.
    void shutdown();

    // Test seam — production uses AppPaths.
    void setSpoolPath(const QString &path) { m_spool.setDatabasePath(path); }

    // Test seam: drives one sampling tick without waiting on the timer.
    void sampleOnceForTest() { onSampleTick(); }

public slots:
    void onLapTimerStateChanged();
    void onSessionSaved();
    // Same signal SessionModel listens to (RaceBoxModel::lapCompleted). Wired
    // in parallel rather than reading SessionModel's state, keeping to the
    // repo's rule that these models talk only through signals — and meaning the
    // `stop` event's lap list is built from the same source the driver's
    // on-screen times come from.
    void onLapCompleted(const RaceBoxLapResult &lap);
    // Re-reads CloudConfig and reconnects. Bound to the settings page so
    // pairing takes effect without a restart.
    void applyConfiguration();

    // Discards the backlog and abandons any open session. A backlog belonging to
    // a car this dash is no longer provisioned for must not be published to
    // whatever it is paired with next. Q_INVOKABLE for tests and for anything
    // that needs the discard on its own; the UNPAIR dialog wants unpair().
    Q_INVOKABLE void clearSpool();

    // Forgets the credential, switches the uplink off, discards the backlog and
    // re-applies the configuration — in that order, which is why it is one call.
    // Bound to the UNPAIR confirmation in CloudUplinkDetail.qml.
    Q_INVOKABLE void unpair();

signals:
    void stateChanged();
    void queuedFramesChanged();
    void sessionActiveChanged();

private slots:
    void onSampleTick();
    void onClientConnected();
    void onClientDisconnected();
    void onClientPublished(int mid);
    void onClientError(const QString &message);

private:
    void setState(State state);
    void setLastError(const QString &message);

    void beginSession();
    void endSession();

    // Starts or stops the 10 Hz sampler from its inputs. Every path that changes
    // one calls this rather than touching the timer.
    void updateSampling();

    QByteArray buildFrame() const;
    QByteArray buildSessionEvent(const QString &event) const;
    // The `{v, sid, frames[]}` backfill message for one drained batch.
    QByteArray buildBackfillEnvelope(const QVector<SpooledFrame> &batch) const;

    void dispatch(const QString &topic, const QByteArray &payload, qint64 seq, int qos);
    void spool(const QString &topic, const QByteArray &payload, qint64 seq);

    void drainNext();
    void refreshQueuedFrames();

    QString telemetryTopic() const;
    QString sessionTopic()   const;
    QString backfillTopic()  const;

    CanDataModel *m_can     = nullptr;
    RaceBoxModel *m_raceBox = nullptr;
    TrackModel   *m_track   = nullptr;
    CloudConfig  *m_config  = nullptr;
    ICloudClient *m_client  = nullptr;

    UplinkSpool m_spool;
    QTimer      m_sampleTimer;

    // Started once, in the constructor, and never restarted. See the class note.
    QElapsedTimer m_mono;

    State   m_state = Disabled;
    QString m_lastError;

    bool    m_started       = false;
    bool    m_sessionActive = false;
    QString m_sid;
    qint64  m_seq = 0;

    // Session summary for the `stop` event, accumulated as the session runs.
    QList<qint64> m_lapTimesMs;
    int           m_topSpeedKmh = 0;

    // Drain bookkeeping. One batch in flight at a time: the next starts on the
    // previous PUBACK, which keeps ordering trivial and stops a large backlog
    // from flooding a marginal link.
    //
    // Grouped rather than three loose members because they only ever have
    // meaning together, and every path that abandons a drain has to clear all
    // three. Spelling that out at five call sites is how one of them came to
    // clear only `active` and leave the other two pointing at a batch that will
    // never be acknowledged.
    struct DrainState {
        bool   active = false;
        int    mid    = -1;   // in-flight publish; matched against published(mid)
        qint64 lastId = -1;   // spool row the PUBACK releases through

        void reset() { *this = DrainState{}; }
    };
    DrainState m_drain;

    // Throttled notify: during a drain this changes 200 at a time and must not
    // spam the QML engine, matching the repo's existing dirty-bit pattern.
    int    m_queuedFrames = 0;
    QTimer m_queuedNotifyTimer;

    static constexpr int kSampleIntervalMs   = 100; // 10 Hz
    static constexpr int kQueuedNotifyMs     = 500;
};

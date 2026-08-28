#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QMetaType>
#include <QCommandLineParser>
#include <QCursor>

#include "src/can/realcanprovider.h"
#include "src/can/mockcanprovider.h"
#include "src/device/sdnotify.h"
#include "src/can/candatamodel.h"
#include "src/device/dashconfig.h"
#include "src/race/raceboxmodel.h"
#include "src/race/mockraceboxprovider.h"
#include "src/race/sessionmodel.h"
#include "src/race/trackmodel.h"
#include "src/core/logging.h"
#include "src/device/logbuffermodel.h"
#include "src/device/devicestatsmodel.h"
#include "src/device/displaymodel.h"
#include "src/device/updatemodel.h"
#include "src/device/networkmodel.h"
#include "src/device/timemodel.h"
#include "src/cloud/cloudconfig.h"
#include "src/cloud/mosquittocloudclient.h"
#include "src/cloud/uplinkmodel.h"

#ifdef HAVE_BLUETOOTH
#include "src/race/raceboxprovider.h"
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Fixes the QStandardPaths AppDataLocation to ~/.local/share/bmw-e46-dash,
    // where all persistent user data lives (see src/apppaths.h). Must be set
    // before any model that persists data is constructed (LogBufferModel below
    // opens its log file immediately).
    QCoreApplication::setApplicationName("bmw-e46-dash");
    qRegisterMetaType<QCanBusFrame>("QCanBusFrame");
    qRegisterMetaType<RaceBoxData>("RaceBoxData");

    // Installs the message handler before anything else logs, so startup
    // messages are captured by the Device Log settings page.
    LogBufferModel logBuffer;

    // Expose RaceBoxModel's enums (e.g. LapTimerState) to QML as `RaceBox.Armed`
    // etc. Uncreatable: QML references the enum values, not instances.
    qmlRegisterUncreatableMetaObject(RaceBoxModel::staticMetaObject, "RaceDash", 1, 0,
                                     "RaceBox", "Enum access only");
    qmlRegisterUncreatableMetaObject(UpdateModel::staticMetaObject, "RaceDash", 1, 0,
                                     "Updates", "Enum access only");
    qmlRegisterUncreatableMetaObject(NetworkModel::staticMetaObject, "RaceDash", 1, 0,
                                     "Net", "Enum access only");
    qmlRegisterUncreatableMetaObject(UplinkModel::staticMetaObject, "RaceDash", 1, 0,
                                     "Uplink", "Enum access only");

    QCommandLineParser parser;
    parser.addOption({"mock",  "Use mock providers (no hardware required)"});
    parser.addOption({"kiosk", "Fullscreen kiosk mode: hide cursor, fill display"});
    parser.process(app);
    const bool useMock   = parser.isSet("mock");
    const bool kioskMode = parser.isSet("kiosk");

    qCInfo(lcApp) << "Starting —"
                  << (useMock ? "mock" : "real") << "providers,"
                  << (kioskMode ? "kiosk" : "windowed") << "mode";

    if (kioskMode)
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

    DashConfig       dashConfig;
    CanDataModel     dataModel;
    RaceBoxModel     raceBoxModel;
    TrackModel       trackModel(&raceBoxModel, useMock);
    SessionModel     sessionModel(&dataModel, &raceBoxModel, &trackModel);
    UpdateModel      updateModel(useMock);
    DeviceStatsModel deviceStatsModel;
    DisplayModel     displayModel;
    NetworkModel     networkModel(useMock);
    TimeModel        timeModel(useMock, &raceBoxModel);

    // Finish lines are owned by TrackModel and applied below via
    // applyStartupFinishLine(). Mock mode seeds a synthetic line (once — skipped
    // if one is already stored for whichever slot this would write to) through
    // the same onFinishLineLearned() path a real learn uses, so it lands in
    // TrackModel (keyed by whatever track is active, or the global "" slot if
    // none is) and shows up on the session map, not just in RaceBoxModel.
    if (useMock && trackModel.finishLineFor(trackModel.activeTrackId()).isEmpty()) {
        double latA, lonA, latB, lonB;
        MockRaceBoxProvider::defaultFinishLine(latA, lonA, latB, lonB);
        trackModel.onFinishLineLearned(latA, lonA, latB, lonB);
        qCInfo(lcApp) << "Mock finish-line gate seeded";
    }

    // CAN provider
    std::unique_ptr<ICanProvider> canProvider;
    if (useMock)
        canProvider = std::make_unique<MockCanProvider>();
    else
        canProvider = std::make_unique<RealCanProvider>();

    QObject::connect(canProvider.get(), &ICanProvider::frameReady,
                     &dataModel, &CanDataModel::onFrame,
                     Qt::QueuedConnection);

    // RaceBox provider
    std::unique_ptr<IRaceBoxProvider> raceBoxProvider;
#ifdef HAVE_BLUETOOTH
    if (!useMock)
        raceBoxProvider = std::make_unique<RaceBoxProvider>(dashConfig.raceBoxDeviceName());
    else
#endif
        raceBoxProvider = std::make_unique<MockRaceBoxProvider>();

    QObject::connect(raceBoxProvider.get(), &IRaceBoxProvider::dataReady,
                     &raceBoxModel, &RaceBoxModel::onData,
                     Qt::QueuedConnection);
    QObject::connect(raceBoxProvider.get(), &IRaceBoxProvider::connectionStateChanged,
                     &raceBoxModel, &RaceBoxModel::onConnectionStateChanged,
                     Qt::QueuedConnection);
    QObject::connect(&raceBoxModel, &RaceBoxModel::speedKmhChanged,
                     &dataModel, &CanDataModel::setSpeed);
    QObject::connect(&sessionModel, &SessionModel::sessionSaved,
                     &raceBoxModel, &RaceBoxModel::resetLapCounters);

    // TrackModel is the single owner of finish lines: it stores one per track id
    // plus a global (no-track) line, persists them to tracks-user.json, and
    // applies the right one to RaceBoxModel on selection / startup. Sector
    // gates ride along the same path (see TrackModel::emitFinishLineFor()), so
    // a track already timed once doesn't waste the first lap of every future
    // session re-deriving gates it already has.
    QObject::connect(&raceBoxModel, &RaceBoxModel::finishLineLearned,
                     &trackModel, &TrackModel::onFinishLineLearned);
    QObject::connect(&trackModel, &TrackModel::applyFinishLine,
                     &raceBoxModel, &RaceBoxModel::setFinishLine);
    QObject::connect(&trackModel, &TrackModel::clearFinishLineRequested,
                     &raceBoxModel, &RaceBoxModel::clearFinishLine);
    QObject::connect(&raceBoxModel, &RaceBoxModel::sectorGatesLearned,
                     &trackModel, &TrackModel::onSectorGatesLearned);
    QObject::connect(&trackModel, &TrackModel::applySectorGates,
                     &raceBoxModel, &RaceBoxModel::setSectorGates);
    trackModel.applyStartupFinishLine();

    // Cloud uplink. Uses the REAL client even under --mock: mock mode is about
    // not needing a CAN adapter and a RaceBox, not about faking the network, and
    // pointing this at a local broker through CloudConfig is how the uplink gets
    // bench-tested. MockCloudClient exists for unit tests only.
    //
    // Construction is cheap by contract — no socket, no SD-card access, no timer
    // — because this is a boot path that is tuned hard. UplinkModel::start()
    // does all of that, and is deferred to the first presented frame below.
    CloudConfig cloudConfig;
    MosquittoCloudClient cloudClient(&cloudConfig);
    UplinkModel uplinkModel(&dataModel, &raceBoxModel, &trackModel,
                            &cloudConfig, &cloudClient);

    // The dash owns the session lifecycle, reusing the signals it already has
    // rather than inventing a second notion of "a run" — so the cloud's session
    // list agrees with what the driver saw on screen.
    QObject::connect(&raceBoxModel, &RaceBoxModel::lapTimerStateChanged,
                     &uplinkModel, &UplinkModel::onLapTimerStateChanged);
    QObject::connect(&sessionModel, &SessionModel::sessionSaved,
                     &uplinkModel, &UplinkModel::onSessionSaved);
    // Same signal SessionModel listens to, wired in parallel: the stop event's
    // lap list is then built from the same source as the on-screen times.
    QObject::connect(&raceBoxModel, &RaceBoxModel::lapCompleted,
                     &uplinkModel, &UplinkModel::onLapCompleted);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("dashConfig",       &dashConfig);
    engine.rootContext()->setContextProperty("dataModel",       &dataModel);
    engine.rootContext()->setContextProperty("raceBoxModel",    &raceBoxModel);
    engine.rootContext()->setContextProperty("sessionModel",    &sessionModel);
    engine.rootContext()->setContextProperty("trackModel",      &trackModel);
    engine.rootContext()->setContextProperty("updateModel",     &updateModel);
    engine.rootContext()->setContextProperty("deviceStatsModel", &deviceStatsModel);
    engine.rootContext()->setContextProperty("displayModel",    &displayModel);
    engine.rootContext()->setContextProperty("networkModel",    &networkModel);
    engine.rootContext()->setContextProperty("timeModel",       &timeModel);
    engine.rootContext()->setContextProperty("logBuffer",       &logBuffer);
    engine.rootContext()->setContextProperty("uplinkModel",     &uplinkModel);
    engine.rootContext()->setContextProperty("cloudConfig",     &cloudConfig);
    engine.rootContext()->setContextProperty("kioskMode",       kioskMode);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    // Tell systemd (Type=notify) we're ready exactly once the first frame has
    // been presented, so its ExecStartPost=plymouth quit can't race ahead of us
    // and leave a black gap between the boot splash and the dashboard.
    // frameSwapped() fires *after* the buffer has been swapped to screen —
    // unlike afterRendering(), which fires before the swap and would let the
    // splash be dismissed a frame early. It's emitted on the render thread, but
    // since the receiver context is the window (main-thread affinity) the call
    // is delivered queued on the main thread; either way sdNotifyReady() only
    // touches a plain OS socket and is safe to call from any thread.
    // Qt::SingleShotConnection (Qt 6.0+) disconnects after the first emission —
    // we only need this once per run.
    // The uplink starts off the same hook, for the same reason: opening the
    // spool is SD-card I/O and connecting is a DNS lookup plus a TLS handshake,
    // and neither may sit between boot and the first frame on screen. Hooking
    // sdNotifyReady's signal rather than adding a timer keeps "nothing on the
    // uplink path runs before the dashboard is visible" true by construction.
    //
    // Queued, not direct: frameSwapped is emitted on the render thread, and
    // unlike sdNotifyReady (which only writes to an OS socket) UplinkModel is
    // main-thread state.
    // TimeModel rides the same hook and for the same reason: its first status
    // read spawns timedatectl (which D-Bus-activates systemd-timedated) and
    // nmcli, and a first-run bootstrap adds an SD-card write. None of that may
    // sit between boot and the first frame, and nothing reads the model until
    // the Date & Time settings page is opened.
    const auto startDeferred = [&uplinkModel, &timeModel] {
        uplinkModel.start();
        timeModel.start();
    };

    // The other end of the same lifecycle. A session is opened by the lap timer
    // but was only ever closed by the driver tapping Save Session, so a stint
    // that ended with the ignition — which is most of them — left the cloud
    // holding a session that never ended, and so never received its lap list or
    // top speed. aboutToQuit fires before the models are destroyed, which is the
    // last point at which a `stop` event can still reach the spool.
    QObject::connect(&app, &QGuiApplication::aboutToQuit,
                     &uplinkModel, [&uplinkModel] { uplinkModel.shutdown(); });

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(window, &QQuickWindow::frameSwapped, window,
                          &sdNotifyReady, Qt::SingleShotConnection);
        QObject::connect(window, &QQuickWindow::frameSwapped, &uplinkModel,
                          startDeferred,
                          Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
    } else {
        sdNotifyReady(); // no window to hook (shouldn't happen) — notify anyway so
                         // systemd doesn't wait out the full start timeout for nothing
        startDeferred();
    }

    canProvider->start();
    raceBoxProvider->start();
    return app.exec();
}

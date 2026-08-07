#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QMetaType>
#include <QCommandLineParser>
#include <QCursor>

#include "src/realcanprovider.h"
#include "src/mockcanprovider.h"
#include "src/sdnotify.h"
#include "src/candatamodel.h"
#include "src/dashconfig.h"
#include "src/raceboxmodel.h"
#include "src/mockraceboxprovider.h"
#include "src/sessionmodel.h"
#include "src/trackmodel.h"
#include "src/logging.h"
#include "src/logbuffermodel.h"
#include "src/devicestatsmodel.h"
#include "src/displaymodel.h"
#include "src/updatemodel.h"
#include "src/networkmodel.h"
#include "src/cloudconfig.h"
#include "src/mosquittocloudclient.h"
#include "src/uplinkmodel.h"

#ifdef HAVE_BLUETOOTH
#include "src/raceboxprovider.h"
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
    // applies the right one to RaceBoxModel on selection / startup.
    QObject::connect(&raceBoxModel, &RaceBoxModel::finishLineLearned,
                     &trackModel, &TrackModel::onFinishLineLearned);
    QObject::connect(&trackModel, &TrackModel::applyFinishLine,
                     &raceBoxModel, &RaceBoxModel::setFinishLine);
    QObject::connect(&trackModel, &TrackModel::clearFinishLineRequested,
                     &raceBoxModel, &RaceBoxModel::clearFinishLine);
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
    const auto startUplink = [&uplinkModel] { uplinkModel.start(); };

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        QObject::connect(window, &QQuickWindow::frameSwapped, window,
                          &sdNotifyReady, Qt::SingleShotConnection);
        QObject::connect(window, &QQuickWindow::frameSwapped, &uplinkModel,
                          startUplink,
                          Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
    } else {
        sdNotifyReady(); // no window to hook (shouldn't happen) — notify anyway so
                         // systemd doesn't wait out the full start timeout for nothing
        startUplink();
    }

    canProvider->start();
    raceBoxProvider->start();
    return app.exec();
}

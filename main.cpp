#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QMetaType>
#include <QCommandLineParser>
#include <QCursor>

#include "src/realcanprovider.h"
#include "src/mockcanprovider.h"
#include "src/candatamodel.h"
#include "src/dashconfig.h"
#include "src/raceboxmodel.h"
#include "src/mockraceboxprovider.h"
#include "src/sessionmodel.h"
#include "src/trackmodel.h"
#include "src/logging.h"
#include "src/logbuffermodel.h"
#include "src/devicestatsmodel.h"
#include "src/updatemodel.h"
#include "src/networkmodel.h"

#ifdef HAVE_BLUETOOTH
#include "src/raceboxprovider.h"
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("dashConfig",       &dashConfig);
    engine.rootContext()->setContextProperty("dataModel",       &dataModel);
    engine.rootContext()->setContextProperty("raceBoxModel",    &raceBoxModel);
    engine.rootContext()->setContextProperty("sessionModel",    &sessionModel);
    engine.rootContext()->setContextProperty("trackModel",      &trackModel);
    engine.rootContext()->setContextProperty("updateModel",     &updateModel);
    engine.rootContext()->setContextProperty("deviceStatsModel", &deviceStatsModel);
    engine.rootContext()->setContextProperty("networkModel",    &networkModel);
    engine.rootContext()->setContextProperty("logBuffer",       &logBuffer);
    engine.rootContext()->setContextProperty("kioskMode",       kioskMode);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    canProvider->start();
    raceBoxProvider->start();
    return app.exec();
}

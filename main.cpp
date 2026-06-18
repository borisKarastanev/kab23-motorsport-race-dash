#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QMetaType>
#include <QCommandLineParser>
#include <QCursor>

#include "src/realcanprovider.h"
#include "src/mockcanprovider.h"
#include "src/candatamodel.h"
#include "src/dashconfig.h"
#include "src/raceboxmodel.h"
#include "src/mockraceboxprovider.h"

#ifdef HAVE_BLUETOOTH
#include "src/raceboxprovider.h"
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    qRegisterMetaType<QCanBusFrame>("QCanBusFrame");
    qRegisterMetaType<RaceBoxData>("RaceBoxData");

    QCommandLineParser parser;
    parser.addOption({"mock",  "Use mock providers (no hardware required)"});
    parser.addOption({"kiosk", "Fullscreen kiosk mode: hide cursor, fill display"});
    parser.process(app);
    const bool useMock   = parser.isSet("mock");
    const bool kioskMode = parser.isSet("kiosk");

    if (kioskMode)
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));

    DashConfig    dashConfig;
    CanDataModel  dataModel;
    RaceBoxModel  raceBoxModel;

    if (useMock) {
        double flLat, flLon, flRadius;
        MockRaceBoxProvider::defaultFinishLine(flLat, flLon, flRadius);
        raceBoxModel.setFinishLine(flLat, flLon, flRadius);
    } else
        raceBoxModel.setFinishLine(dashConfig.finishLineLat(),
                                   dashConfig.finishLineLon(),
                                   dashConfig.finishLineRadiusM());

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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("dashConfig",   &dashConfig);
    engine.rootContext()->setContextProperty("dataModel",    &dataModel);
    engine.rootContext()->setContextProperty("raceBoxModel", &raceBoxModel);
    engine.rootContext()->setContextProperty("kioskMode",    kioskMode);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    canProvider->start();
    raceBoxProvider->start();
    return app.exec();
}

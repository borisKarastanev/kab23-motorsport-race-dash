#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QMetaType>

#include "src/realcanprovider.h"
#include "src/candatamodel.h"
#include "src/dashconfig.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    qRegisterMetaType<QCanBusFrame>("QCanBusFrame");

    DashConfig dashConfig;
    CanDataModel dataModel;
    RealCanProvider provider;

    // QueuedConnection ensures cross-thread safety when provider moves to a worker thread
    QObject::connect(&provider, &RealCanProvider::frameReady,
                     &dataModel, &CanDataModel::onFrame,
                     Qt::QueuedConnection);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("dashConfig", &dashConfig);
    engine.rootContext()->setContextProperty("dataModel", &dataModel);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    provider.start();
    return app.exec();
}

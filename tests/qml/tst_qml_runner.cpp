#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <QQmlContext>
#include <QObject>

// Stand-in for the real DisplayModel context property: DisplaySettings.qml
// reads `displayModel` as a bare context property, same as every other model
// in this app, so it needs *something* registered under that name to load
// standalone in Qt Quick Test without the full app/main.cpp wiring.
class MockDisplayModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int  brightness   READ brightness   WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(bool hasBacklight READ hasBacklight CONSTANT)

public:
    explicit MockDisplayModel(QObject *parent = nullptr) : QObject(parent) {}

    int  brightness() const { return m_brightness; }
    void setBrightness(int v)
    {
        if (v == m_brightness)
            return;
        m_brightness = v;
        emit brightnessChanged();
    }
    bool hasBacklight() const { return true; }

signals:
    void brightnessChanged();

private:
    int m_brightness = 50;
};

class Setup : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty("displayModel", new MockDisplayModel(engine));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(DashQmlTests, Setup)

#include "tst_qml_runner.moc"

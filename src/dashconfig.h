#pragma once

#include <QObject>

class DashConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(int ledCount        READ ledCount        CONSTANT)
    Q_PROPERTY(int greenStart      READ greenStart      CONSTANT)
    Q_PROPERTY(int yellowStart     READ yellowStart     CONSTANT)
    Q_PROPERTY(int redStart        READ redStart        CONSTANT)
    Q_PROPERTY(int flashStart      READ flashStart      CONSTANT)
    Q_PROPERTY(int flashIntervalMs READ flashIntervalMs CONSTANT)

public:
    explicit DashConfig(QObject *parent = nullptr);

    int ledCount()        const { return m_ledCount; }
    int greenStart()      const { return m_greenStart; }
    int yellowStart()     const { return m_yellowStart; }
    int redStart()        const { return m_redStart; }
    int flashStart()      const { return m_flashStart; }
    int flashIntervalMs() const { return m_flashIntervalMs; }

private:
    int m_ledCount        = 10;
    int m_greenStart      = 4500;
    int m_yellowStart     = 6000;
    int m_redStart        = 6500;
    int m_flashStart      = 6750;
    int m_flashIntervalMs = 80;
};

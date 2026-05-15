#pragma once

#include <QObject>
#include <QCanBusFrame>

class ICanProvider : public QObject {
    Q_OBJECT
public:
    explicit ICanProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~ICanProvider() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void frameReady(const QCanBusFrame &frame);
};

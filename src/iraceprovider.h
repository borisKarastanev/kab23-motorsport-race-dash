#pragma once

#include "raceboxdata.h"
#include <QObject>

class IRaceBoxProvider : public QObject {
    Q_OBJECT
public:
    explicit IRaceBoxProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IRaceBoxProvider() = default;

    virtual void start() = 0;
    virtual void stop()  = 0;

signals:
    void dataReady(const RaceBoxData &data);
    void connectionStateChanged(bool connected);
};

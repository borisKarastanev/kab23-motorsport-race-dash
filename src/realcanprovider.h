#pragma once

#include "icanprovider.h"

class RealCanProvider : public ICanProvider {
    Q_OBJECT
public:
    explicit RealCanProvider(QObject *parent = nullptr);

    void start() override;
    void stop() override;
};

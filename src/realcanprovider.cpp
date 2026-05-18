#include "realcanprovider.h"

RealCanProvider::RealCanProvider(QObject *parent)
    : ICanProvider(parent) {}

// SocketCAN implementation wired up in Phase 5 (Raspberry Pi deployment)
void RealCanProvider::start() {}
void RealCanProvider::stop()  {}

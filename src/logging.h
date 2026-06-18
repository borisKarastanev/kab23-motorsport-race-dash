#pragma once
#include <QLoggingCategory>

// dash.app   — startup, config, finish line persistence
// dash.can   — SocketCAN interface lifecycle and errors
// dash.racebox — BLE connection, GPS fix, lap events

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcCan)
Q_DECLARE_LOGGING_CATEGORY(lcRaceBox)

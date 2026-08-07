#pragma once
#include <QLoggingCategory>

// dash.app   — startup, config, finish line persistence
// dash.can   — SocketCAN interface lifecycle and errors
// dash.racebox — BLE connection, GPS fix, lap events
// dash.uplink       — cloud uplink: pairing config, session lifecycle, drain
// dash.uplink.spool — store-and-forward spool (SQLite FIFO)
// dash.uplink.mqtt  — libmosquitto client: connect, TLS, publish

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcCan)
Q_DECLARE_LOGGING_CATEGORY(lcRaceBox)
Q_DECLARE_LOGGING_CATEGORY(lcUplink)
Q_DECLARE_LOGGING_CATEGORY(lcSpool)
Q_DECLARE_LOGGING_CATEGORY(lcCloud)

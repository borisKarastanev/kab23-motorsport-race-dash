#pragma once

#include <QStandardPaths>
#include <QDir>
#include <QString>

// Single source of truth for where the dashboard stores persistent user data
// (recorded sessions, favourites/finish lines, settings, the downloaded track
// DB, logs).
//
// Historically each model wrote to QCoreApplication::applicationDirPath(),
// which on this deployment resolves to the build/ directory next to the binary.
// That made user data collateral damage of any build operation: a plain
// `rm -rf build` — or install.sh's clean rebuild — destroyed irreplaceable race
// data. Data now lives in a stable per-user location (QStandardPaths
// AppDataLocation, i.e. ~/.local/share/bmw-e46-dash on Linux) that no build
// step ever touches. install.sh performs a one-time migration of any legacy
// build/ files into this directory on update.
//
// Requires QCoreApplication::setApplicationName() to have been called (done in
// main.cpp) so AppDataLocation resolves to a stable, app-specific path.
namespace AppPaths {

// Absolute path to the user-data directory, created if it does not yet exist.
inline QString dataDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

// Absolute path to a named file inside the user-data directory. The directory
// is guaranteed to exist by the time this returns, so callers can open the
// path for writing directly.
inline QString dataFile(const QString &name)
{
    return dataDir() + '/' + name;
}

} // namespace AppPaths

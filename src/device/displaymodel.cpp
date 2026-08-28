#include "src/device/displaymodel.h"
#include "src/core/apppaths.h"
#include "src/core/logging.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <cmath>

DisplayModel::DisplayModel(const QString &backlightRoot, QObject *parent)
    : QObject(parent)
{
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(kPersistDebounceMs);
    connect(&m_persistTimer, &QTimer::timeout, this, &DisplayModel::persist);

    discoverBacklight(backlightRoot);
    load();
    applyToHardware(); // match the panel to the saved setting on every startup
}

DisplayModel::~DisplayModel()
{
    // Flush a debounced write the timer hasn't fired yet, so the last value
    // isn't lost if the app exits between a slider move and the timer firing.
    if (m_persistPending)
        persist();
}

void DisplayModel::discoverBacklight(const QString &backlightRoot)
{
    QDir root(backlightRoot);
    if (!root.exists())
        return;

    // Device node name varies by kernel (rpi_backlight vs 10-0045 on
    // Bookworm's panel driver) — glob for the first directory that actually
    // has both files rather than assuming either name.
    for (const QString &entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString candidate = root.filePath(entry);
        QFile maxFile(candidate + "/max_brightness");
        if (!QFile::exists(candidate + "/brightness") || !maxFile.open(QIODevice::ReadOnly))
            continue;

        bool ok = false;
        const int max = maxFile.readAll().trimmed().toInt(&ok);
        if (!ok || max <= 0)
            continue;

        m_backlightDevicePath = candidate;
        m_maxBrightness = max;
        return;
    }

    qCInfo(lcApp) << "No backlight device found under" << backlightRoot
                  << "— brightness control unavailable (expected off-Pi)";
}

void DisplayModel::applyToHardware() const
{
    if (!hasBacklight())
        return;

    const int floor = static_cast<int>(std::lround(m_maxBrightness * kMinHardwareFraction));
    const int hw = floor + static_cast<int>(std::lround((m_brightnessPct / 100.0) * (m_maxBrightness - floor)));

    QFile f(m_backlightDevicePath + "/brightness");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcApp) << "Could not write brightness to" << f.fileName();
        return;
    }
    f.write(QByteArray::number(hw));
}

QString DisplayModel::configPath()
{
    return AppPaths::dataFile("display.conf");
}

void DisplayModel::load()
{
    QSettings s(configPath(), QSettings::IniFormat);
    const int saved = s.value("Display/Brightness", m_brightnessPct).toInt();
    m_brightnessPct = qBound(0, saved, 100);
}

void DisplayModel::persist()
{
    QSettings s(configPath(), QSettings::IniFormat);
    s.setValue("Display/Brightness", m_brightnessPct);
    s.sync();
    m_persistPending = false;
}

void DisplayModel::setBrightness(int pct)
{
    const int clamped = qBound(0, pct, 100);
    if (clamped == m_brightnessPct)
        return;

    m_brightnessPct = clamped;
    applyToHardware(); // live: cheap sysfs write, smooth slider preview
    // Debounce the (SD-card-costly) persist so a full drag writes display.conf
    // once, not once per intermediate value. (restart() coalesces.)
    m_persistPending = true;
    m_persistTimer.start();
    emit brightnessChanged();
}

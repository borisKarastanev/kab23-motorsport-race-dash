#include "dashconfig.h"
#include "apppaths.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QDebug>

static QString validatedPosition(const QString &pos, const QString &key)
{
    if (pos == "left" || pos == "center" || pos == "right")
        return pos;
    qWarning() << "dashboard.conf: invalid position" << pos << "for" << key << "— defaulting to \"left\"";
    return "left";
}

static QString configPath()
{
    return AppPaths::dataFile("dashboard.conf");
}

static void writeDefaultConfig(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&f);
    out <<
        "# BMW E46 Dashboard Configuration\n"
        "# Edit values below and restart the application to apply.\n"
        "\n"
        "[LedStrip]\n"
        "\n"
        "# Total number of LEDs in the shift-light strip\n"
        "LedCount = 10\n"
        "\n"
        "# Flash blink half-period in milliseconds (lower = faster flash)\n"
        "FlashIntervalMs = 80\n"
        "\n"
        "# RPM thresholds per pair, outside-in (pair 0 = outermost, pair 4 = centre)\n"
        "# Pairs 0-1 light green, pairs 2-3 light yellow, pair 4 lights red and flashes\n"
        "Pair0Rpm = 5800\n"
        "Pair1Rpm = 6000\n"
        "Pair2Rpm = 6200\n"
        "Pair3Rpm = 6500\n"
        "Pair4Rpm = 6600\n"
        "\n"
        "# RPM at which all LEDs switch to blue and flash together\n"
        "AllBlueRpm = 6750\n"
        "\n"
        "# Limiter RPM (informational)\n"
        "LimiterRpm = 6800\n"
        "\n"
        "\n"
        "[Gauges]\n"
        "\n"
        "# Visible: true / false\n"
        "# Position: left | center | right | bottom\n"
        "\n"
        "# Gear indicator — shown in the center panel (position is always center)\n"
        "Gear.Visible = false\n"
        "\n"
        "RPM.Visible = true\n"
        "RPM.Position = left\n"
        "\n"
        "Speed.Visible = true\n"
        "Speed.Position = center\n"
        "\n"
        "Coolant.Visible = true\n"
        "Coolant.Position = left\n"
        "\n"
        "OilTemp.Visible = true\n"
        "OilTemp.Position = right\n"
        "\n"
        "# Lap timer panel (shown in the bottom zone)\n"
        "LapTimer.Visible = true\n"
        "\n"
        "\n"
        "[RaceBox]\n"
        "\n"
        "# BLE device name prefix — must match the start of the RaceBox Mini device name\n"
        "DeviceName = RaceBox Mini\n";
    // Finish lines (global + per-track) are owned by TrackModel and persisted
    // in tracks-user.json — not configured here.
}

DashConfig::DashConfig(QObject *parent) : QObject(parent)
{
    const QString path = configPath();

    if (!QFile::exists(path))
        writeDefaultConfig(path);

    QSettings s(path, QSettings::IniFormat);

    s.beginGroup("LedStrip");
    m_ledCount        = s.value("LedCount",        m_ledCount).toInt();
    m_flashIntervalMs = s.value("FlashIntervalMs", m_flashIntervalMs).toInt();
    m_pair0Rpm        = s.value("Pair0Rpm",        m_pair0Rpm).toInt();
    m_pair1Rpm        = s.value("Pair1Rpm",        m_pair1Rpm).toInt();
    m_pair2Rpm        = s.value("Pair2Rpm",        m_pair2Rpm).toInt();
    m_pair3Rpm        = s.value("Pair3Rpm",        m_pair3Rpm).toInt();
    m_pair4Rpm        = s.value("Pair4Rpm",        m_pair4Rpm).toInt();
    m_allBlueRpm      = s.value("AllBlueRpm",      m_allBlueRpm).toInt();
    m_limiterRpm      = s.value("LimiterRpm",      m_limiterRpm).toInt();
    s.endGroup();

    s.beginGroup("Gauges");
    m_gearVisible     = s.value("Gear.Visible",     m_gearVisible).toBool();
    m_rpmVisible      = s.value("RPM.Visible",      m_rpmVisible).toBool();
    m_rpmPosition     = validatedPosition(s.value("RPM.Position",     m_rpmPosition).toString(),     "RPM.Position");
    m_speedVisible    = s.value("Speed.Visible",    m_speedVisible).toBool();
    m_speedPosition   = validatedPosition(s.value("Speed.Position",   m_speedPosition).toString(),   "Speed.Position");
    m_coolantVisible  = s.value("Coolant.Visible",  m_coolantVisible).toBool();
    m_coolantPosition = validatedPosition(s.value("Coolant.Position", m_coolantPosition).toString(), "Coolant.Position");
    m_oilTempVisible  = s.value("OilTemp.Visible",  m_oilTempVisible).toBool();
    m_oilTempPosition = validatedPosition(s.value("OilTemp.Position", m_oilTempPosition).toString(), "OilTemp.Position");
    m_lapTimerVisible = s.value("LapTimer.Visible", m_lapTimerVisible).toBool();
    s.endGroup();

    s.beginGroup("RaceBox");
    m_raceBoxDeviceName  = s.value("DeviceName", m_raceBoxDeviceName).toString();
    s.endGroup();
}

#include "dashconfig.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTextStream>

static QString configPath()
{
    return QCoreApplication::applicationDirPath() + "/dashboard.conf";
}

static void writeDefaultConfig(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&f);
    out <<
        "# BMW E46 Dashboard — LED Strip Configuration\n"
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
        "LimiterRpm = 6800\n";
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
}

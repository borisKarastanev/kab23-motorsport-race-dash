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
        "# RPM at which the first LED lights (green zone begins)\n"
        "GreenStart = 4500\n"
        "\n"
        "# RPM at which all LEDs are lit and colour switches to yellow\n"
        "YellowStart = 6000\n"
        "\n"
        "# RPM at which colour switches from yellow to red\n"
        "RedStart = 6500\n"
        "\n"
        "# RPM at which all LEDs start flashing\n"
        "FlashStart = 6750\n"
        "\n"
        "# Flash blink half-period in milliseconds (lower = faster flash)\n"
        "FlashIntervalMs = 80\n";
}

DashConfig::DashConfig(QObject *parent) : QObject(parent)
{
    const QString path = configPath();

    if (!QFile::exists(path))
        writeDefaultConfig(path);

    QSettings s(path, QSettings::IniFormat);
    s.beginGroup("LedStrip");
    m_ledCount        = s.value("LedCount",        m_ledCount).toInt();
    m_greenStart      = s.value("GreenStart",      m_greenStart).toInt();
    m_yellowStart     = s.value("YellowStart",     m_yellowStart).toInt();
    m_redStart        = s.value("RedStart",        m_redStart).toInt();
    m_flashStart      = s.value("FlashStart",      m_flashStart).toInt();
    m_flashIntervalMs = s.value("FlashIntervalMs", m_flashIntervalMs).toInt();
    s.endGroup();
}

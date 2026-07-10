#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// Screen brightness for the Device Settings > Display page. Targets the Linux
// sysfs backlight interface (/sys/class/backlight/*/brightness) for the
// official Raspberry Pi 7" DSI touchscreen — the device node name varies by
// kernel (rpi_backlight on older stacks, 10-0045 on Bookworm's panel driver),
// so the constructor discovers it rather than hardcoding either name.
//
// On hardware with no backlight device (e.g. this dev machine), brightness()
// still tracks and persists a value so the Settings slider works, but every
// hardware write is skipped — see hasBacklight().
class DisplayModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int  brightness   READ brightness   WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(bool hasBacklight READ hasBacklight CONSTANT)

public:
    // backlightRoot is a constructor parameter (not a hardcoded path) so tests
    // can point it at a temporary fake sysfs tree instead of the real one.
    explicit DisplayModel(const QString &backlightRoot = QStringLiteral("/sys/class/backlight"),
                          QObject *parent = nullptr);
    ~DisplayModel() override;

    int  brightness()   const { return m_brightnessPct; }
    void setBrightness(int pct);

    bool hasBacklight() const { return !m_backlightDevicePath.isEmpty(); }

signals:
    void brightnessChanged();

private:
    // Finds the first backlight device directory under backlightRoot that has
    // both a brightness and max_brightness file, reading the latter into
    // m_maxBrightness. Leaves m_backlightDevicePath empty if none is found.
    void discoverBacklight(const QString &backlightRoot);

    // Scales m_brightnessPct (0-100) to the device's raw hardware range and
    // writes it to <m_backlightDevicePath>/brightness. No-op if hasBacklight()
    // is false.
    void applyToHardware() const;

    static QString configPath();
    void load();
    void persist(); // writes m_brightnessPct to display.conf; clears m_persistPending

    QString m_backlightDevicePath; // empty if no backlight device was found
    int     m_maxBrightness = 0;   // raw hardware max, read from max_brightness

    // A headless kiosk dash has no keyboard to recover a fully-blacked-out
    // screen, so 0% still leaves the panel at this fraction of its raw range
    // rather than truly off.
    static constexpr double kMinHardwareFraction = 0.10;

    // Persistence is debounced: the sysfs write happens live on every
    // setBrightness (for smooth slider preview), but the QSettings write to
    // display.conf — a full .ini rewrite + fsync, costly on the Pi's SD card —
    // is coalesced so a whole slider drag results in a single write shortly
    // after movement settles. The destructor flushes any pending write so a
    // value can't be lost if the app exits mid-debounce.
    static constexpr int kPersistDebounceMs = 500;
    QTimer m_persistTimer;
    bool   m_persistPending = false;

    int m_brightnessPct = 80; // overwritten by load() if a saved value exists
};

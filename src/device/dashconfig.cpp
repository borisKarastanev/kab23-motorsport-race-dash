#include "src/device/dashconfig.h"
#include "src/core/apppaths.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QDebug>
#include <cmath>

namespace {

const QStringList &allPositions()
{
    static const QStringList toks = {
        "top-left",    "top-center",    "top-right",
        "center-left", "center",        "center-right",
        "bottom-left", "bottom-center", "bottom-right"
    };
    return toks;
}

// Fixed priority order used both to resolve load-time collisions (earlier
// entities keep their cell, later ones get bumped) and to scan for a free
// cell. Mirrors the pre-grid default layout (RPM/Speed/OilTemp were the
// "top" row, Coolant the middle, LapTimer the bottom).
const QStringList &entityPriorityOrder()
{
    static const QStringList order = { "rpm", "speed", "coolant", "oiltemp", "gear", "laptimer" };
    return order;
}

// Old config used a 3-column left|center|right scheme with no row concept.
// Migrating combines that column with a fixed row per entity (see callers)
// so a legacy value always lands on a sane, entity-specific default cell.
QString migrateLegacyColumn(const QString &oldToken, const QString &row)
{
    if (oldToken == "left")   return row + "-left";
    if (oldToken == "right")  return row + "-right";
    if (oldToken == "center") return row == "center" ? "center" : row + "-center";
    return QString(); // not a recognized legacy token
}

// Resolves a stored position value to a valid 9-cell token.
//
// The bare token "center" is inherently ambiguous between the two formats:
// it's a legacy *column* name AND, spelled identically, the new-format token
// for the exact middle cell. There is no way to disambiguate it from the
// value alone, so we key off the config's schema version instead of guessing
// per value:
//   - legacy config (schema < 2): raw is a left|center|right column, so
//     migrate first (combining it with the entity's fixed row).
//   - current config (schema >= 2): raw is already a 9-cell token, so a bare
//     "center" means the middle cell and must be kept, never migrated.
QString resolvePosition(const QString &raw, const QString &row,
                        const QString &fallback, const QString &key, bool legacy)
{
    if (legacy) {
        const QString migrated = migrateLegacyColumn(raw, row);
        if (allPositions().contains(migrated))
            return migrated;
        // fall through — tolerate an already-compound value in a v1 file
    }
    if (allPositions().contains(raw))
        return raw;
    qWarning() << "dashboard.conf: invalid position" << raw << "for" << key
               << "— defaulting to" << fallback;
    return fallback;
}

// Factory ratio = default threshold / default limiter (6800). Anchoring to
// the factory defaults (not the current values) keeps repeated limiter edits
// idempotent and ordering monotonic.
constexpr double kPair0Ratio   = 5800.0 / 6800.0;
constexpr double kPair1Ratio   = 6000.0 / 6800.0;
constexpr double kPair2Ratio   = 6200.0 / 6800.0;
constexpr double kPair3Ratio   = 6500.0 / 6800.0;
constexpr double kPair4Ratio   = 6600.0 / 6800.0;
constexpr double kAllBlueRatio = 6750.0 / 6800.0;

constexpr int kMinLimiterRpm = 5000;
constexpr int kMaxLimiterRpm = 12000; // covers high-revving NA/motorcycle-derived engines

// dashboard.conf schema version. Bumped from the implicit "1" (the old
// left|center|right column scheme) to "2" (the 9-cell grid). Written by
// persist()/writeDefaultConfig(); a config with no SchemaVersion is treated
// as legacy so its column positions are migrated to grid cells on load.
constexpr int kSchemaVersion = 2;

// Broad sanity bound for hand-tuned temp thresholds — not a hardware limit,
// just guards against runaway stepper taps.
constexpr double kMinThresholdTemp = 0.0;
constexpr double kMaxThresholdTemp = 300.0;

QString configPath()
{
    return AppPaths::dataFile("dashboard.conf");
}

void writeDefaultConfig(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&f);
    out <<
        "# BMW E46 Dashboard Configuration\n"
        "# Edit from Settings > Dashboard Layout on-device — this file is\n"
        "# rewritten (without comments) on every change made there.\n"
        "\n"
        "SchemaVersion = 2\n"
        "\n"
        "[LedStrip]\n"
        "\n"
        "LedCount = 10\n"
        "FlashIntervalMs = 80\n"
        "\n"
        "# Limiter is the master control; Pair0-4/AllBlue are derived from it\n"
        "# proportionally and stored here only for inspection.\n"
        "Pair0Rpm = 5800\n"
        "Pair1Rpm = 6000\n"
        "Pair2Rpm = 6200\n"
        "Pair3Rpm = 6500\n"
        "Pair4Rpm = 6600\n"
        "AllBlueRpm = 6750\n"
        "LimiterRpm = 6800\n"
        "\n"
        "\n"
        "[Gauges]\n"
        "\n"
        "# Position: top-left | top-center | top-right | center-left | center |\n"
        "#           center-right | bottom-left | bottom-center | bottom-right\n"
        "\n"
        "Gear.Visible = false\n"
        "Gear.Position = center\n"
        "\n"
        "RPM.Visible = true\n"
        "RPM.Position = top-left\n"
        "\n"
        "Speed.Visible = true\n"
        "Speed.Position = top-center\n"
        "\n"
        "Coolant.Visible = true\n"
        "Coolant.Position = center-left\n"
        "Coolant.WarningTemp = 95\n"
        "Coolant.DangerTemp = 105\n"
        "\n"
        "OilTemp.Visible = true\n"
        "OilTemp.Position = top-right\n"
        "OilTemp.WarningTemp = 120\n"
        "OilTemp.DangerTemp = 135\n"
        "\n"
        "LapTimer.Visible = true\n"
        "LapTimer.Position = bottom-left\n"
        "\n"
        "\n"
        "[RaceBox]\n"
        "\n"
        "# BLE device name prefix — must match the start of the RaceBox Mini device name\n"
        "DeviceName = RaceBox Mini\n";
    // Finish lines (global + per-track) are owned by TrackModel and persisted
    // in tracks-user.json — not configured here.
}

} // namespace

DashConfig::DashConfig(QObject *parent) : QObject(parent)
{
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(kPersistDebounceMs);
    connect(&m_persistTimer, &QTimer::timeout, this, &DashConfig::persist);

    load();
}

DashConfig::~DashConfig()
{
    // Flush a debounced write the timer hasn't fired yet, so the last change
    // isn't lost if the app exits between an edit and the timer firing.
    if (m_persistPending)
        persist();
}

void DashConfig::load()
{
    const QString path = configPath();
    if (!QFile::exists(path))
        writeDefaultConfig(path);

    QSettings s(path, QSettings::IniFormat);

    // A config with no SchemaVersion predates the 9-cell grid, so its stored
    // positions are legacy left|center|right columns needing migration.
    const bool legacy = s.value("SchemaVersion", 1).toInt() < kSchemaVersion;

    s.beginGroup("LedStrip");
    m_ledCount        = s.value("LedCount",        m_ledCount).toInt();
    m_flashIntervalMs = s.value("FlashIntervalMs", m_flashIntervalMs).toInt();
    m_limiterRpm       = qBound(kMinLimiterRpm, s.value("LimiterRpm", m_limiterRpm).toInt(), kMaxLimiterRpm);
    s.endGroup();
    recomputePairThresholds(); // pairs are always derived from the limiter, never read back

    s.beginGroup("Gauges");
    m_gearVisible     = s.value("Gear.Visible",     m_gearVisible).toBool();
    m_gearPosition    = resolvePosition(s.value("Gear.Position",    m_gearPosition).toString(),    "center", m_gearPosition,    "Gear.Position",    legacy);
    m_rpmVisible      = s.value("RPM.Visible",      m_rpmVisible).toBool();
    m_rpmPosition     = resolvePosition(s.value("RPM.Position",     m_rpmPosition).toString(),     "top",    m_rpmPosition,     "RPM.Position",     legacy);
    m_speedVisible    = s.value("Speed.Visible",    m_speedVisible).toBool();
    m_speedPosition   = resolvePosition(s.value("Speed.Position",   m_speedPosition).toString(),   "top",    m_speedPosition,   "Speed.Position",   legacy);
    m_coolantVisible  = s.value("Coolant.Visible",  m_coolantVisible).toBool();
    m_coolantPosition = resolvePosition(s.value("Coolant.Position", m_coolantPosition).toString(), "center", m_coolantPosition, "Coolant.Position", legacy);
    m_oilTempVisible  = s.value("OilTemp.Visible",  m_oilTempVisible).toBool();
    m_oilTempPosition = resolvePosition(s.value("OilTemp.Position", m_oilTempPosition).toString(), "top",    m_oilTempPosition, "OilTemp.Position", legacy);
    m_lapTimerVisible  = s.value("LapTimer.Visible",  m_lapTimerVisible).toBool();
    m_lapTimerPosition = resolvePosition(s.value("LapTimer.Position", m_lapTimerPosition).toString(), "bottom", m_lapTimerPosition, "LapTimer.Position", legacy);

    m_coolantWarningTemp = qBound(kMinThresholdTemp, s.value("Coolant.WarningTemp", m_coolantWarningTemp).toDouble(), kMaxThresholdTemp);
    m_coolantDangerTemp  = qBound(kMinThresholdTemp, s.value("Coolant.DangerTemp",  m_coolantDangerTemp).toDouble(),  kMaxThresholdTemp);
    m_oilWarningTemp      = qBound(kMinThresholdTemp, s.value("OilTemp.WarningTemp", m_oilWarningTemp).toDouble(), kMaxThresholdTemp);
    m_oilDangerTemp        = qBound(kMinThresholdTemp, s.value("OilTemp.DangerTemp",  m_oilDangerTemp).toDouble(), kMaxThresholdTemp);
    s.endGroup();

    // load() clamps each threshold independently; a hand-edited or partially
    // upgraded config can still store them inverted. The runtime setters keep
    // warning < danger, so restore that invariant here too (clamp the warning
    // just below danger, mirroring setCoolant/OilWarningTemp).
    if (m_coolantWarningTemp >= m_coolantDangerTemp)
        m_coolantWarningTemp = m_coolantDangerTemp - 1.0;
    if (m_oilWarningTemp >= m_oilDangerTemp)
        m_oilWarningTemp = m_oilDangerTemp - 1.0;

    s.beginGroup("RaceBox");
    m_raceBoxDeviceName  = s.value("DeviceName", m_raceBoxDeviceName).toString();
    s.endGroup();

    // Defensive normalization: a hand-edited config, or a migration from the
    // old 3-column scheme, can still land two visible entities on the same
    // cell. resolveLoadedCollisions() (not assignFreeCellIfOccupied() per
    // entity) is required here — see its comment for why.
    resolveLoadedCollisions();
}

void DashConfig::persist()
{
    QSettings s(configPath(), QSettings::IniFormat);

    s.setValue("SchemaVersion", kSchemaVersion);

    s.beginGroup("LedStrip");
    s.setValue("LedCount",        m_ledCount);
    s.setValue("FlashIntervalMs", m_flashIntervalMs);
    s.setValue("Pair0Rpm",        m_pair0Rpm);
    s.setValue("Pair1Rpm",        m_pair1Rpm);
    s.setValue("Pair2Rpm",        m_pair2Rpm);
    s.setValue("Pair3Rpm",        m_pair3Rpm);
    s.setValue("Pair4Rpm",        m_pair4Rpm);
    s.setValue("AllBlueRpm",      m_allBlueRpm);
    s.setValue("LimiterRpm",      m_limiterRpm);
    s.endGroup();

    s.beginGroup("Gauges");
    s.setValue("Gear.Visible",       m_gearVisible);
    s.setValue("Gear.Position",      m_gearPosition);
    s.setValue("RPM.Visible",        m_rpmVisible);
    s.setValue("RPM.Position",       m_rpmPosition);
    s.setValue("Speed.Visible",      m_speedVisible);
    s.setValue("Speed.Position",     m_speedPosition);
    s.setValue("Coolant.Visible",    m_coolantVisible);
    s.setValue("Coolant.Position",   m_coolantPosition);
    s.setValue("Coolant.WarningTemp", m_coolantWarningTemp);
    s.setValue("Coolant.DangerTemp",  m_coolantDangerTemp);
    s.setValue("OilTemp.Visible",    m_oilTempVisible);
    s.setValue("OilTemp.Position",   m_oilTempPosition);
    s.setValue("OilTemp.WarningTemp", m_oilWarningTemp);
    s.setValue("OilTemp.DangerTemp",  m_oilDangerTemp);
    s.setValue("LapTimer.Visible",   m_lapTimerVisible);
    s.setValue("LapTimer.Position",  m_lapTimerPosition);
    s.endGroup();

    s.beginGroup("RaceBox");
    s.setValue("DeviceName", m_raceBoxDeviceName);
    s.endGroup();

    s.sync();
    m_persistPending = false;
}

void DashConfig::schedulePersist()
{
    // Debounce the (SD-card-costly) full-file rewrite so a burst of edits —
    // several stepper taps, a swap, a toggle — costs one write, not one per
    // change. The destructor flushes any pending write on exit.
    m_persistPending = true;
    m_persistTimer.start();
}

void DashConfig::recomputePairThresholds()
{
    m_pair0Rpm   = qRound(m_limiterRpm * kPair0Ratio);
    m_pair1Rpm   = qRound(m_limiterRpm * kPair1Ratio);
    m_pair2Rpm   = qRound(m_limiterRpm * kPair2Ratio);
    m_pair3Rpm   = qRound(m_limiterRpm * kPair3Ratio);
    m_pair4Rpm   = qRound(m_limiterRpm * kPair4Ratio);
    m_allBlueRpm = qRound(m_limiterRpm * kAllBlueRatio);
}

void DashConfig::setLimiterRpm(int rpm)
{
    const int clamped = qBound(kMinLimiterRpm, rpm, kMaxLimiterRpm);
    if (clamped == m_limiterRpm)
        return;
    m_limiterRpm = clamped;
    recomputePairThresholds();
    emit limiterRpmChanged();
    emit pairThresholdsChanged();
    schedulePersist();
}

void DashConfig::setGearVisible(bool v)
{
    if (v == m_gearVisible) return;
    m_gearVisible = v;
    if (v) assignFreeCellIfOccupied("gear");
    emit gearVisibleChanged();
    bumpLayoutRevision();
    schedulePersist();
}

void DashConfig::setRpmVisible(bool v)
{
    if (v == m_rpmVisible) return;
    m_rpmVisible = v;
    if (v) assignFreeCellIfOccupied("rpm");
    emit rpmVisibleChanged();
    bumpLayoutRevision();
    schedulePersist();
}

void DashConfig::setSpeedVisible(bool v)
{
    if (v == m_speedVisible) return;
    m_speedVisible = v;
    if (v) assignFreeCellIfOccupied("speed");
    emit speedVisibleChanged();
    bumpLayoutRevision();
    schedulePersist();
}

void DashConfig::setCoolantVisible(bool v)
{
    if (v == m_coolantVisible) return;
    m_coolantVisible = v;
    if (v) assignFreeCellIfOccupied("coolant");
    emit coolantVisibleChanged();
    bumpLayoutRevision();
    schedulePersist();
}

void DashConfig::setOilTempVisible(bool v)
{
    if (v == m_oilTempVisible) return;
    m_oilTempVisible = v;
    if (v) assignFreeCellIfOccupied("oiltemp");
    emit oilTempVisibleChanged();
    bumpLayoutRevision();
    schedulePersist();
}

void DashConfig::setLapTimerVisible(bool v)
{
    if (v == m_lapTimerVisible) return;
    m_lapTimerVisible = v;
    if (v) assignFreeCellIfOccupied("laptimer");
    emit lapTimerVisibleChanged();
    bumpLayoutRevision();
    schedulePersist();
}

void DashConfig::setCoolantWarningTemp(double v)
{
    const double clamped = qBound(kMinThresholdTemp, v, m_coolantDangerTemp - 1.0);
    if (qFuzzyCompare(clamped, m_coolantWarningTemp)) return;
    m_coolantWarningTemp = clamped;
    emit coolantWarningTempChanged();
    schedulePersist();
}

void DashConfig::setCoolantDangerTemp(double v)
{
    const double clamped = qBound(m_coolantWarningTemp + 1.0, v, kMaxThresholdTemp);
    if (qFuzzyCompare(clamped, m_coolantDangerTemp)) return;
    m_coolantDangerTemp = clamped;
    emit coolantDangerTempChanged();
    schedulePersist();
}

void DashConfig::setOilWarningTemp(double v)
{
    const double clamped = qBound(kMinThresholdTemp, v, m_oilDangerTemp - 1.0);
    if (qFuzzyCompare(clamped, m_oilWarningTemp)) return;
    m_oilWarningTemp = clamped;
    emit oilWarningTempChanged();
    schedulePersist();
}

void DashConfig::setOilDangerTemp(double v)
{
    const double clamped = qBound(m_oilWarningTemp + 1.0, v, kMaxThresholdTemp);
    if (qFuzzyCompare(clamped, m_oilDangerTemp)) return;
    m_oilDangerTemp = clamped;
    emit oilDangerTempChanged();
    schedulePersist();
}

bool DashConfig::isVisible(const QString &entity) const
{
    if (entity == "rpm")      return m_rpmVisible;
    if (entity == "speed")    return m_speedVisible;
    if (entity == "coolant")  return m_coolantVisible;
    if (entity == "oiltemp")  return m_oilTempVisible;
    if (entity == "gear")     return m_gearVisible;
    if (entity == "laptimer") return m_lapTimerVisible;
    return false;
}

QString DashConfig::positionFor(const QString &entity) const
{
    if (entity == "rpm")      return m_rpmPosition;
    if (entity == "speed")    return m_speedPosition;
    if (entity == "coolant")  return m_coolantPosition;
    if (entity == "oiltemp")  return m_oilTempPosition;
    if (entity == "gear")     return m_gearPosition;
    if (entity == "laptimer") return m_lapTimerPosition;
    return QString();
}

void DashConfig::setPositionRaw(const QString &entity, const QString &pos)
{
    if (entity == "rpm")           { m_rpmPosition = pos;      emit rpmPositionChanged(); }
    else if (entity == "speed")    { m_speedPosition = pos;    emit speedPositionChanged(); }
    else if (entity == "coolant")  { m_coolantPosition = pos;  emit coolantPositionChanged(); }
    else if (entity == "oiltemp")  { m_oilTempPosition = pos;  emit oilTempPositionChanged(); }
    else if (entity == "gear")     { m_gearPosition = pos;     emit gearPositionChanged(); }
    else if (entity == "laptimer") { m_lapTimerPosition = pos; emit lapTimerPositionChanged(); }
    else return;
    bumpLayoutRevision();
}

void DashConfig::bumpLayoutRevision()
{
    ++m_layoutRevision;
    emit layoutRevisionChanged();
}

QString DashConfig::entityAtPosition(const QString &pos, const QString &excludeEntity) const
{
    for (const QString &entity : entityPriorityOrder()) {
        if (entity == excludeEntity) continue;
        if (isVisible(entity) && positionFor(entity) == pos)
            return entity;
    }
    return QString();
}

void DashConfig::assignFreeCellIfOccupied(const QString &entity)
{
    if (!isVisible(entity)) return;

    const QString cur = positionFor(entity);
    if (entityAtPosition(cur, entity).isEmpty())
        return; // no collision — keep the current cell

    for (const QString &candidate : allPositions()) {
        if (entityAtPosition(candidate, entity).isEmpty()) {
            setPositionRaw(entity, candidate);
            return;
        }
    }
    // Unreachable with <= 9 visible entities sharing 9 cells (we only ever
    // have 6), but leave the entity where it is rather than crash if it did.
}

void DashConfig::resolveLoadedCollisions()
{
    // Deliberately NOT implemented as assignFreeCellIfOccupied() per entity:
    // that helper (correctly) treats *any* other visible entity currently
    // sitting on the same cell as a blocker, which is safe only when every
    // other entity is already known-collision-free (true for the single-
    // entity runtime callers: an enable-toggle or a swap). Right after
    // load(), multiple entities can share a cell simultaneously (e.g. a
    // stale config, or two legacy tokens migrating to the same default) —
    // scanning "is anyone else here" against still-unresolved entities would
    // let a not-yet-processed entity bump an earlier, rightfully-placed one.
    // This walks priority order accumulating an explicit taken set instead,
    // so only entities that lose a genuine conflict move, and the earliest
    // in priority order always keeps its loaded cell.
    QStringList taken;
    for (const QString &entity : entityPriorityOrder()) {
        if (!isVisible(entity)) continue;
        QString cur = positionFor(entity);
        if (taken.contains(cur)) {
            for (const QString &candidate : allPositions()) {
                if (!taken.contains(candidate)) {
                    cur = candidate;
                    break;
                }
            }
            setPositionRaw(entity, cur);
        }
        taken.append(cur);
    }
}

void DashConfig::setEntityPosition(const QString &entity, const QString &pos)
{
    if (!allPositions().contains(pos)) return;
    const QString oldPos = positionFor(entity);
    if (oldPos.isEmpty() || oldPos == pos) return;

    const QString occupant = entityAtPosition(pos, entity);
    if (!occupant.isEmpty())
        setPositionRaw(occupant, oldPos); // swap the occupant into entity's old cell
    setPositionRaw(entity, pos);
    schedulePersist();
}

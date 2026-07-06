#include "logbuffermodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QStringList>
#include <QTextStream>
#include <utility>

LogBufferModel *LogBufferModel::s_instance = nullptr;
QtMessageHandler LogBufferModel::s_previousHandler = nullptr;

namespace {
thread_local bool g_inHandler = false;
}

QString LogBufferModel::logPath()
{
    return QCoreApplication::applicationDirPath() + "/dash.log";
}

QString LogBufferModel::rotatedLogPath()
{
    return QCoreApplication::applicationDirPath() + "/dash.log.1";
}

LogBufferModel::LogBufferModel(QObject *parent) : QObject(parent)
{
    s_instance = this;

    loadPersisted();

    m_logFile.setFileName(logPath());
    m_logFile.open(QIODevice::Append | QIODevice::Text);
    m_logBytes = m_logFile.size();

    s_previousHandler = qInstallMessageHandler(&LogBufferModel::messageHandler);
}

LogBufferModel::~LogBufferModel()
{
    qInstallMessageHandler(s_previousHandler);
    s_instance = nullptr;
}

std::deque<LogBufferModel::Entry> &LogBufferModel::bucketFor(const QString &level)
{
    if (level == "warn")
        return m_warn;
    if (level == "error")
        return m_error;
    return m_info;
}

const std::deque<LogBufferModel::Entry> &LogBufferModel::bucketFor(const QString &level) const
{
    if (level == "warn")
        return m_warn;
    if (level == "error")
        return m_error;
    return m_info;
}

void LogBufferModel::pushCapped(std::deque<Entry> &bucket, const Entry &e)
{
    bucket.push_back(e);
    if (bucket.size() > static_cast<size_t>(kCapacity))
        bucket.pop_front();
}

void LogBufferModel::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (s_instance && !g_inHandler) {
        g_inHandler = true;
        s_instance->handleMessage(type, context, msg);
        g_inHandler = false;
    }

    if (s_previousHandler)
        s_previousHandler(type, context, msg);
}

void LogBufferModel::handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString level;
    switch (type) {
    case QtDebugMsg:
    case QtInfoMsg:
        level = "info";
        break;
    case QtWarningMsg:
        level = "warn";
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        level = "error";
        break;
    }

    const QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");

    const bool visible = appendEntry(level, category, msg, false);
    persistEntry(level, category, msg);

    // Only refresh QML when the entry lands in the bucket currently on screen —
    // an "error" line shouldn't invalidate the list while "info" is selected.
    if (visible)
        QMetaObject::invokeMethod(this, &LogBufferModel::entriesChanged, Qt::QueuedConnection);
}

bool LogBufferModel::appendEntry(const QString &level, const QString &category, const QString &message, bool prev)
{
    QMutexLocker locker(&m_mutex);

    Entry e;
    e.time     = QDateTime::currentDateTime().toString("HH:mm:ss");
    e.category = category;
    e.message  = message;
    e.prev     = prev;

    pushCapped(bucketFor(level), e);
    return level == m_filterLevel;
}

void LogBufferModel::persistEntry(const QString &level, const QString &category, const QString &message)
{
    QMutexLocker locker(&m_mutex);

    if (!m_logFile.isOpen())
        return;

    QJsonObject obj;
    obj["t"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    obj["l"] = level;
    obj["c"] = category;
    obj["m"] = message;

    m_logBytes += m_logFile.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    m_logBytes += m_logFile.write("\n");
    m_logFile.flush(); // flush per message so an abrupt power cut keeps the log

    rotateIfNeeded();
}

void LogBufferModel::rotateIfNeeded()
{
    // Tracked byte counter instead of a size() stat on every message.
    if (m_logBytes < kRotateBytes)
        return;

    m_logFile.close();
    QFile::remove(rotatedLogPath());
    QFile::rename(logPath(), rotatedLogPath());
    m_logFile.setFileName(logPath());
    m_logFile.open(QIODevice::Append | QIODevice::Text);
    m_logBytes = 0;
}

void LogBufferModel::loadPersisted()
{
    // Collect raw lines (cheap I/O) oldest-file-first, then JSON-parse only from
    // the newest backwards, stopping once every bucket is full — so a large log
    // isn't fully parsed on the startup path just to keep the last 20 per level.
    QStringList lines;
    for (const QString &path : { rotatedLogPath(), logPath() }) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream in(&f);
        while (!in.atEnd())
            lines.append(in.readLine());
    }

    // Newest-first temporaries; reversed into the members afterwards so the
    // deques stay in chronological (oldest-front) order.
    std::deque<Entry> info, warn, error;
    auto full = [](const std::deque<Entry> &d) { return d.size() >= static_cast<size_t>(kCapacity); };

    for (int i = lines.size() - 1; i >= 0; --i) {
        if (full(info) && full(warn) && full(error))
            break;

        const QString &line = lines.at(i);
        if (line.trimmed().isEmpty())
            continue;

        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        std::deque<Entry> &bucket = obj["l"].toString() == "warn"  ? warn
                                  : obj["l"].toString() == "error" ? error
                                  :                                  info;
        if (full(bucket))
            continue;

        const QDateTime dt = QDateTime::fromString(obj["t"].toString(), Qt::ISODate);
        Entry e;
        e.time     = dt.isValid() ? dt.toString("HH:mm:ss") : QString();
        e.category = obj["c"].toString();
        e.message  = obj["m"].toString();
        e.prev     = true;
        bucket.push_back(e); // newest-first for now
    }

    for (auto pair : { std::pair(&info, &m_info), std::pair(&warn, &m_warn), std::pair(&error, &m_error) })
        pair.second->assign(pair.first->rbegin(), pair.first->rend());
}

QVariantMap LogBufferModel::toVariant(const Entry &e)
{
    QVariantMap m;
    m["time"]     = e.time;
    m["category"] = e.category;
    m["message"]  = e.message;
    m["prev"]     = e.prev;
    return m;
}

void LogBufferModel::setFilterLevel(const QString &level)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_filterLevel == level)
            return;
        m_filterLevel = level;
    }
    emit filterLevelChanged();
    emit entriesChanged();
}

QVariantList LogBufferModel::filteredEntries() const
{
    QMutexLocker locker(&m_mutex);

    const std::deque<Entry> &bucket = bucketFor(m_filterLevel);

    QVariantList result;
    for (const Entry &e : bucket)
        result.append(toVariant(e));
    return result;
}

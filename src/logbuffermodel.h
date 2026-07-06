#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QMutex>
#include <QFile>
#include <deque>

// Installs a Qt message handler and buffers the last 20 messages per level
// (info / warn / error) for display in the Device Log settings page. Messages
// are also appended to <applicationDirPath>/dash.log as JSON lines so the log
// survives app restarts and crashes; the previous run's entries are loaded on
// startup and marked with "prev": true. The default handler is chained so
// stderr/journald output is unaffected.
class LogBufferModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterLevelChanged)
    Q_PROPERTY(QVariantList filteredEntries READ filteredEntries NOTIFY entriesChanged)

public:
    explicit LogBufferModel(QObject *parent = nullptr);
    ~LogBufferModel() override;

    QString filterLevel() const { QMutexLocker l(&m_mutex); return m_filterLevel; }
    void setFilterLevel(const QString &level);
    QVariantList filteredEntries() const;

signals:
    void filterLevelChanged();
    void entriesChanged();

private:
    struct Entry {
        QString time;
        QString category;
        QString message;
        bool prev = false;
    };

    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    void handleMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    // Appends under the mutex; returns true if the entry lands in the currently
    // selected filter bucket (so the caller knows whether QML needs refreshing).
    bool appendEntry(const QString &level, const QString &category, const QString &message, bool prev);
    void loadPersisted();
    void persistEntry(const QString &level, const QString &category, const QString &message);
    void rotateIfNeeded();
    static QString logPath();
    static QString rotatedLogPath();
    static QVariantMap toVariant(const Entry &e);
    static void pushCapped(std::deque<Entry> &bucket, const Entry &e);
    std::deque<Entry> &bucketFor(const QString &level);
    const std::deque<Entry> &bucketFor(const QString &level) const;

    mutable QMutex m_mutex;
    std::deque<Entry> m_info, m_warn, m_error;
    QString m_filterLevel = "info";
    QFile m_logFile;
    qint64 m_logBytes = 0;

    static LogBufferModel *s_instance;
    static QtMessageHandler s_previousHandler;

    static constexpr int kCapacity = 20;
    static constexpr qint64 kRotateBytes = 256 * 1024;
};

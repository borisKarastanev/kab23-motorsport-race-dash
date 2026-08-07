#include "uplinkspool.h"
#include "apppaths.h"
#include "logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <algorithm>

UplinkSpool::UplinkSpool(QObject *parent)
    : QObject(parent)
{
    // A unique connection name per instance so tests can hold several spools at
    // once, and so a re-open after clear() never collides with a stale Qt SQL
    // connection registered under the default name.
    m_connectionName = QStringLiteral("uplink-spool-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

UplinkSpool::~UplinkSpool()
{
    abandonConnection();
}

void UplinkSpool::abandonConnection()
{
    if (m_db.isOpen())
        m_db.close();
    // The QSqlDatabase copy must be released before removeDatabase(), or Qt
    // warns "connection is still in use, all queries will cease to work".
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
    m_rowCount = 0;
}

void UplinkSpool::setDatabasePath(const QString &path)
{
    m_path = path;
    m_openFailed = false;
}

bool UplinkSpool::open()
{
    if (m_open)
        return true;
    // Latched: see the header. Retrying a permanent failure at 10 Hz is a log
    // flood and continuous SD-card writes, not a recovery.
    if (m_openFailed)
        return false;

    if (m_path.isEmpty())
        m_path = AppPaths::dataFile(QStringLiteral("uplink-spool.sqlite"));

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_path);

    if (!m_db.open()) {
        // The driver name is worth saying out loud: on Pi OS the SQLite driver
        // plugin ships in libqt6sql6-sqlite, packaged separately from the Qt SQL
        // headers, so a machine that builds fine can still fail here at runtime.
        qCWarning(lcSpool) << "could not open spool at" << m_path
                           << "-" << m_db.lastError().text()
                           << "(drivers:" << QSqlDatabase::drivers() << ')'
                           << "— spooling is disabled for this run";
        m_openFailed = true;
        abandonConnection();
        return false;
    }

    QSqlQuery pragma(m_db);
    // WAL: readers never block the 10 Hz writer, and a crash rolls back to the
    // last commit rather than corrupting the file.
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    // NORMAL rather than FULL: FULL fsyncs on every commit, which on an SD card
    // is both slow and a direct contributor to wear. NORMAL can lose the last
    // transaction on a power cut — acceptable, because that is at most one
    // batch of frames the cloud will accept again on re-send anyway.
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    if (!ensureSchema()) {
        m_openFailed = true;
        abandonConnection();
        return false;
    }

    m_open = true;
    // The only COUNT(*) in this class. Everything after this maintains the
    // counter incrementally — see count().
    m_rowCount = countFromDb();
    qCInfo(lcSpool) << "spool ready at" << m_path << "rows:" << m_rowCount;
    return true;
}

int UplinkSpool::countFromDb()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM frames")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

bool UplinkSpool::ensureSchema()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS frames ("
            "  id      INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  topic   TEXT    NOT NULL,"
            "  sid     TEXT    NOT NULL,"
            "  seq     INTEGER NOT NULL,"
            "  payload BLOB    NOT NULL)"))) {
        qCWarning(lcSpool) << "schema failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool UplinkSpool::append(const SpooledFrame &frame)
{
    return append(QVector<SpooledFrame>{ frame });
}

bool UplinkSpool::append(const QVector<SpooledFrame> &frames)
{
    if (frames.isEmpty())
        return true;
    if (!open())
        return false;

    if (!m_db.transaction()) {
        qCWarning(lcSpool) << "could not begin transaction:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO frames (topic, sid, seq, payload) VALUES (?, ?, ?, ?)"));

    for (const SpooledFrame &frame : frames) {
        q.addBindValue(frame.topic);
        q.addBindValue(frame.sid);
        q.addBindValue(frame.seq);
        q.addBindValue(frame.payload);
        if (!q.exec()) {
            qCWarning(lcSpool) << "insert failed:" << q.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        qCWarning(lcSpool) << "commit failed:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    m_rowCount += frames.size();
    evictOverflow();
    return true;
}

void UplinkSpool::evictOverflow()
{
    if (m_rowCount <= kMaxRows)
        return;

    const int excess = m_rowCount - kMaxRows;
    QSqlQuery q(m_db);
    // Oldest first — ascending id. Deliberately not "newest first": the driver
    // is looking for the last stint, not the first.
    q.prepare(QStringLiteral(
        "DELETE FROM frames WHERE id IN "
        "(SELECT id FROM frames ORDER BY id ASC LIMIT ?)"));
    q.addBindValue(excess);
    if (!q.exec()) {
        qCWarning(lcSpool) << "eviction failed:" << q.lastError().text();
        return;
    }
    // numRowsAffected() rather than `excess`: the counter must track what the
    // database actually did, otherwise a partial delete leaves it drifting and
    // every later eviction is sized off a wrong number.
    m_rowCount -= std::max(0, q.numRowsAffected());
    qCWarning(lcSpool) << "spool full — evicted" << excess << "oldest frames";
}

QVector<SpooledFrame> UplinkSpool::peek(int limit)
{
    QVector<SpooledFrame> out;
    if (!open())
        return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, topic, sid, seq, payload FROM frames ORDER BY id ASC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec()) {
        qCWarning(lcSpool) << "peek failed:" << q.lastError().text();
        return out;
    }

    out.reserve(limit);
    while (q.next()) {
        SpooledFrame frame;
        frame.id      = q.value(0).toLongLong();
        frame.topic   = q.value(1).toString();
        frame.sid     = q.value(2).toString();
        frame.seq     = q.value(3).toLongLong();
        frame.payload = q.value(4).toByteArray();
        out.append(frame);
    }
    return out;
}

QVector<SpooledFrame> UplinkSpool::peekRun(int limit)
{
    QVector<SpooledFrame> rows = peek(limit);
    if (rows.isEmpty())
        return rows;

    // Truncate at the first change of topic or sid. The next drain picks up
    // where this one stopped, so nothing is skipped — the batch is just split
    // at the boundary it has to be split at.
    //
    // In place, on the vector peek() already built. Copying the leading run into
    // a second vector meant 200 SpooledFrame copies (and the refcount traffic on
    // their QString/QByteArray members) per drain batch, on the GUI thread, to
    // produce what is usually the same 200 rows: a batch straddles a boundary
    // only twice per session.
    const QString topic = rows.first().topic;
    const QString sid   = rows.first().sid;

    for (qsizetype i = 1; i < rows.size(); ++i) {
        if (rows.at(i).topic != topic || rows.at(i).sid != sid) {
            rows.resize(i);
            break;
        }
    }
    return rows;
}

bool UplinkSpool::releaseThrough(qint64 lastId)
{
    if (!open())
        return false;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM frames WHERE id <= ?"));
    q.addBindValue(lastId);
    if (!q.exec()) {
        qCWarning(lcSpool) << "release failed:" << q.lastError().text();
        return false;
    }
    m_rowCount -= std::max(0, q.numRowsAffected());
    return true;
}

bool UplinkSpool::clear()
{
    if (!open())
        return false;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM frames"))) {
        qCWarning(lcSpool) << "clear failed:" << q.lastError().text();
        return false;
    }
    m_rowCount = 0;
    return true;
}

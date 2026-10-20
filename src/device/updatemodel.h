#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QProcess>
#include <QFile>
#include <functional>

class QNetworkAccessManager;

// Orchestrates checking for and installing app updates. Updates are
// git-tag based: the highest semver tag on origin (discovered via
// `git ls-remote --tags`) is compared against the version baked into the
// binary at build time (DASH_VERSION, from CMake PROJECT_VERSION). Installing
// checks out the new tag and runs install.sh under sudo, piping a
// once-per-operation password collected via a QML modal — nothing is stored
// on disk. The password is wiped as soon as install.sh exits; the follow-up
// reboot needs none (polkit rule from install.sh). See
// qml/settings/AppVersionDetail.qml for the state -> UI mapping.
class UpdateModel : public QObject {
    Q_OBJECT
public:
    enum State {
        Idle,
        CheckingConnection,
        NoConnection,
        ReadyToCheck,
        Checking,
        CheckFailed,
        UpToDate,
        UpdateAvailable,
        AwaitingPassword,
        ValidatingPassword,
        WrongPassword,
        Installing,
        InstallFailed,
        InstallSucceeded,
        Rebooting
    };
    Q_ENUM(State)

    Q_PROPERTY(QString currentVersion  READ currentVersion  CONSTANT)
    Q_PROPERTY(QString latestVersion   READ latestVersion   NOTIFY stateChanged)
    Q_PROPERTY(State   state           READ state           NOTIFY stateChanged)
    Q_PROPERTY(QString errorText       READ errorText       NOTIFY stateChanged)
    Q_PROPERTY(QString installStage    READ installStage    NOTIFY installProgressChanged)
    Q_PROPERTY(double  installProgress READ installProgress NOTIFY installProgressChanged)

    explicit UpdateModel(bool mockMode, QObject *parent = nullptr);

    QString currentVersion() const;
    QString latestVersion()  const { return m_latestVersion; }
    State   state()          const { return m_state; }
    QString errorText()      const { return m_errorText; }
    QString installStage()   const { return m_installStage; }
    double  installProgress() const { return m_installProgress; }

    // Counts install.sh's own step "..." calls, given its source text. Pure
    // (no file I/O) so it's directly unit-testable; countInstallStages() below
    // is the file-reading wrapper actually used at runtime.
    static int countStageMarkers(const QString &scriptText);
    // True for install.sh's summary lines (per-step timings, build jobs,
    // ccache stats, total time) — the ones copied into the Device Log.
    static bool isInstallSummaryLine(const QString &line);
    // Appends `chunk` to `buffer` and removes and returns the complete lines
    // (trailing '\r' stripped); an unterminated tail stays in `buffer`.
    static QStringList takeCompleteLines(QString &buffer, const QString &chunk);

    Q_INVOKABLE void checkConnection();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void startInstall();
    Q_INVOKABLE void submitPassword(const QString &password);
    Q_INVOKABLE void cancelPassword();
    Q_INVOKABLE void requestReboot();
    Q_INVOKABLE void reset();

signals:
    void stateChanged();
    void installProgressChanged();

private:
    QString repoRoot() const;
    void setState(State s, const QString &error = QString());
    void zeroPassword();

    // Real (unmocked) pipeline steps
    void runGitLsRemote();
    void runRollbackCapture();
    void runFetch();
    void runCheckout();
    void runValidatePassword();
    void runInstallScript();
    int  countInstallStages() const;
    void openInstallLog();
    void appendInstallLog(const QString &text);
    void closeInstallLog(const QString &trailer);
    void logInstallOutput(const QString &chunk);
    void flushInstallOutput();
    void handleInstallLine(const QString &line);
    void onRebootFailed(const QString &output);
    void runRollbackCheckout(const QString &reason);
    void beginInstalling();
    void startProcessStep(const QString &program, const QStringList &args, int timeoutMs,
                          std::function<void(bool ok, const QString &output)> onDone,
                          bool feedPassword = false,
                          std::function<void(const QString &chunk)> onOutput = {});

    // Mock pipeline
    void mockRunInstallStages(int stageIndex);

    bool m_mockMode;
    State m_state = Idle;
    QString m_latestVersion;
    QString m_errorText;
    QString m_installStage;
    double m_installProgress = 0.0;

    QString m_rollbackRef;
    QString m_targetTag;
    QByteArray m_password;

    QNetworkAccessManager *m_nam = nullptr;
    QProcess *m_proc = nullptr;
    QString m_procOutputTail;
    QFile   m_installLog;     // <data dir>/update.log — full output of the last install.sh run
    QString m_installLineBuf; // unterminated tail of the last output chunk
    int m_installStagesSeen = 0;

    // Recomputed by countInstallStages() right before each real install run
    // (after runCheckout() has already updated install.sh on disk to the
    // version being installed), rather than a second hardcoded count that
    // drifts every time a step is added to or removed from install.sh.
    // kInstallTotalStagesFallback is only used if that count ever comes back
    // as 0 (unreadable file, unexpected format) so installProgress can't
    // divide by zero.
    int m_installTotalStages = kInstallTotalStagesFallback;
    static constexpr int kInstallTotalStagesFallback = 8;
};

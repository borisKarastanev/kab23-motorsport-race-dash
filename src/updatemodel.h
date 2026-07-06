#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QProcess>
#include <functional>

class QNetworkAccessManager;

// Orchestrates checking for and installing app updates. Updates are
// git-tag based: the highest semver tag on origin (discovered via
// `git ls-remote --tags`) is compared against the version baked into the
// binary at build time (DASH_VERSION, from CMake PROJECT_VERSION). Installing
// checks out the new tag and runs install.sh under sudo, piping a
// once-per-operation password collected via a QML modal — nothing is stored
// on disk. See qml/settings/AppVersionDetail.qml for the state -> UI mapping.
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
    int m_installStagesSeen = 0;

    static constexpr int kInstallTotalStages = 8;
};

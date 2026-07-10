#include "updatemodel.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QSet>
#include <QTimer>
#include <memory>

#ifndef DASH_VERSION
#define DASH_VERSION "0.0.0"
#endif

namespace {

QString lastLines(const QString &text, int n)
{
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    if (lines.size() <= n)
        return lines.join('\n');
    return lines.mid(lines.size() - n).join('\n');
}

const QStringList kMockInstallStages = {
    "Installing dependencies",
    "Building application",
    "Enabling Bluetooth adapter",
    "Enabling persistent journald logging",
    "Installing systemd CAN service",
    "Installing dashboard system service (console/eglfs mode)",
    "Installing XDG autostart (desktop/Wayland/X11 mode)",
    "Done"
};

}

UpdateModel::UpdateModel(bool mockMode, QObject *parent)
    : QObject(parent)
    , m_mockMode(mockMode)
{
}

QString UpdateModel::currentVersion() const
{
    return QStringLiteral(DASH_VERSION);
}

QString UpdateModel::repoRoot() const
{
    // The binary lives in <repo>/build/bmw-e46-dash
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir.absolutePath();
}

void UpdateModel::setState(State s, const QString &error)
{
    m_state = s;
    m_errorText = error;
    emit stateChanged();
}

void UpdateModel::zeroPassword()
{
    m_password.fill('\0');
    m_password.clear();
}

// ── connection check ────────────────────────────────────────────────

void UpdateModel::checkConnection()
{
    setState(CheckingConnection);

    if (m_mockMode) {
        QTimer::singleShot(800, this, [this]() { setState(ReadyToCheck); });
        return;
    }

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest req(QUrl("https://github.com"));
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_nam->head(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            setState(ReadyToCheck);
        else
            setState(NoConnection, reply->errorString());
    });
}

// ── tag check ────────────────────────────────────────────────────────

void UpdateModel::checkForUpdates()
{
    if (m_state != ReadyToCheck && m_state != UpToDate && m_state != CheckFailed)
        return;

    setState(Checking);

    if (m_mockMode) {
        QTimer::singleShot(1200, this, [this]() {
            if (qEnvironmentVariable("DASH_MOCK_UPDATE") == "latest") {
                setState(UpToDate);
            } else {
                m_latestVersion = "v9.9.9";
                m_targetTag     = "v9.9.9";
                setState(UpdateAvailable);
            }
        });
        return;
    }

    runGitLsRemote();
}

void UpdateModel::runGitLsRemote()
{
    startProcessStep("git", { "ls-remote", "--tags", "origin" }, 15000,
        [this](bool ok, const QString &output) {
            if (!ok) {
                setState(CheckFailed, "Could not reach GitHub (git ls-remote failed)");
                return;
            }

            static const QRegularExpression tagLine(R"(refs/tags/([^\s\^]+)(\^\{\})?)");
            QSet<QString> names;
            for (const QString &line : output.split('\n')) {
                const QRegularExpressionMatch m = tagLine.match(line);
                if (m.hasMatch())
                    names.insert(m.captured(1));
            }

            static const QRegularExpression semverRe(R"(^v?\d+(\.\d+){0,2}$)");
            QVersionNumber best;
            QString bestName;
            for (const QString &name : names) {
                if (!semverRe.match(name).hasMatch())
                    continue;
                QString numeric = name;
                if (numeric.startsWith('v'))
                    numeric.remove(0, 1);
                const QVersionNumber v = QVersionNumber::fromString(numeric);
                if (best.isNull() || QVersionNumber::compare(v, best) > 0) {
                    best = v;
                    bestName = name;
                }
            }

            if (bestName.isEmpty()) {
                qCInfo(lcApp) << "No release tags on origin yet";
                setState(UpToDate);
                return;
            }

            const QVersionNumber current = QVersionNumber::fromString(currentVersion());
            if (QVersionNumber::compare(best, current) > 0) {
                m_latestVersion = bestName.startsWith('v') ? bestName : ("v" + bestName);
                m_targetTag     = bestName;
                setState(UpdateAvailable);
            } else {
                setState(UpToDate);
            }
        });
}

// ── install orchestration ────────────────────────────────────────────

void UpdateModel::startInstall()
{
    if (m_state != UpdateAvailable)
        return;
    setState(AwaitingPassword);
}

void UpdateModel::submitPassword(const QString &password)
{
    if (m_state != AwaitingPassword && m_state != WrongPassword)
        return;

    m_password = password.toUtf8();
    setState(ValidatingPassword);

    if (m_mockMode) {
        QTimer::singleShot(600, this, [this]() {
            if (m_password == QByteArray("pass")) {
                beginInstalling();
                mockRunInstallStages(0);
            } else {
                zeroPassword();
                setState(WrongPassword, "Incorrect password");
            }
        });
        return;
    }

    runRollbackCapture();
}

void UpdateModel::cancelPassword()
{
    if (m_state != AwaitingPassword && m_state != WrongPassword)
        return;
    zeroPassword();
    setState(UpdateAvailable);
}

void UpdateModel::runRollbackCapture()
{
    startProcessStep("git", { "rev-parse", "HEAD" }, 15000,
        [this](bool ok, const QString &output) {
            if (!ok) {
                zeroPassword();
                setState(InstallFailed, "Could not determine current version (git rev-parse failed)");
                return;
            }
            m_rollbackRef = output.trimmed();
            runFetch();
        });
}

void UpdateModel::runFetch()
{
    startProcessStep("git", { "fetch", "--tags", "--force", "origin" }, 60000,
        [this](bool ok, const QString &output) {
            if (!ok) {
                zeroPassword();
                setState(InstallFailed, "git fetch failed: " + lastLines(output, 15));
                return;
            }
            runCheckout();
        });
}

void UpdateModel::runCheckout()
{
    startProcessStep("git", { "checkout", "-f", m_targetTag }, 15000,
        [this](bool ok, const QString &output) {
            if (!ok) {
                zeroPassword();
                setState(InstallFailed, "git checkout failed: " + lastLines(output, 15));
                return;
            }
            runValidatePassword();
        });
}

void UpdateModel::runValidatePassword()
{
    startProcessStep("sudo", { "-S", "-k", "-p", "", "-v" }, 15000,
        [this](bool ok, const QString &) {
            if (!ok) {
                zeroPassword();
                setState(WrongPassword, "Incorrect password");
                return;
            }
            beginInstalling();
            runInstallScript();
        }, /*feedPassword=*/true);
}

void UpdateModel::beginInstalling()
{
    m_installStagesSeen = 0;
    m_installStage.clear();
    m_installProgress = 0.0;
    emit installProgressChanged();
    setState(Installing);
}

int UpdateModel::countStageMarkers(const QString &scriptText)
{
    static const QRegularExpression stepCall(R"(^step ".+"$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = stepCall.globalMatch(scriptText);
    int count = 0;
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

int UpdateModel::countInstallStages() const
{
    // Counts install.sh's own step "..." calls rather than hardcoding a
    // number that has to be kept in sync by hand — it previously drifted
    // badly (a stale constant of 8 against install.sh's real, growing step
    // count), which made the progress bar hit 100% width well before the
    // script actually finished while the stage-name label kept updating
    // underneath it. Called after runCheckout(), so this reads the version
    // of install.sh actually about to run, not the currently-installed one.
    QFile f(repoRoot() + "/install.sh");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return kInstallTotalStagesFallback;

    const int count = countStageMarkers(QString::fromUtf8(f.readAll()));
    return count > 0 ? count : kInstallTotalStagesFallback;
}

void UpdateModel::runInstallScript()
{
    m_installTotalStages = countInstallStages();

    // No timeout: install.sh runs apt + a full build and can take minutes.
    // Progress is reported by parsing the "=== stage ===" markers it prints.
    startProcessStep("sudo", { "-S", "-p", "", "bash", repoRoot() + "/install.sh" }, 0,
        [this](bool ok, const QString &output) {
            m_procOutputTail = lastLines(output, 15);
            if (ok) {
                zeroPassword();
                setState(InstallSucceeded);
            } else {
                runRollbackCheckout("install.sh failed: " + m_procOutputTail);
            }
        },
        /*feedPassword=*/true,
        [this](const QString &chunk) {
            static const QRegularExpression marker(R"(^=== (.+) ===$)", QRegularExpression::MultilineOption);
            QRegularExpressionMatchIterator it = marker.globalMatch(chunk);
            while (it.hasNext()) {
                m_installStagesSeen++;
                m_installStage = it.next().captured(1);
                m_installProgress = qMin(1.0, double(m_installStagesSeen) / m_installTotalStages);
                emit installProgressChanged();
            }
        });
}

void UpdateModel::runRollbackCheckout(const QString &reason)
{
    zeroPassword();
    startProcessStep("git", { "checkout", "-f", m_rollbackRef }, 15000,
        [this, reason](bool ok, const QString &) {
            const QString suffix = ok
                ? " — rolled back to the previous version."
                : " — rollback ALSO FAILED; manual recovery needed.";
            setState(InstallFailed, reason + suffix);
        });
}

void UpdateModel::requestReboot()
{
    if (m_state != InstallSucceeded)
        return;

    setState(Rebooting);

    if (m_mockMode) {
        qCInfo(lcApp) << "[mock] reboot requested — no-op";
        return;
    }

    startProcessStep("sudo", { "-S", "-p", "", "reboot" }, 10000,
        [](bool, const QString &) {
            // The system is rebooting (or the command failed); there is no
            // meaningful follow-up action once the reboot has been issued.
        }, /*feedPassword=*/true);
    zeroPassword();
}

void UpdateModel::reset()
{
    if (m_state == Installing || m_state == Rebooting)
        return;

    zeroPassword();
    m_latestVersion.clear();
    m_errorText.clear();
    m_installStage.clear();
    m_installProgress = 0.0;
    m_targetTag.clear();
    m_rollbackRef.clear();
    setState(Idle);
}

// ── generic async process step ───────────────────────────────────────

void UpdateModel::startProcessStep(const QString &program, const QStringList &args, int timeoutMs,
                                   std::function<void(bool, const QString &)> onDone,
                                   bool feedPassword,
                                   std::function<void(const QString &)> onOutput)
{
    // The state machine only starts a step after the previous step's finished
    // handler has run and nulled m_proc, so this should never fire mid-run.
    Q_ASSERT(!m_proc || m_proc->state() == QProcess::NotRunning);
    if (m_proc) {
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    QProcess *proc = new QProcess(this);
    m_proc = proc;
    proc->setWorkingDirectory(repoRoot());
    proc->setProcessChannelMode(QProcess::MergedChannels);

    auto outputBuf = std::make_shared<QString>();
    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, outputBuf, onOutput]() {
        const QString chunk = QString::fromUtf8(proc->readAllStandardOutput());
        *outputBuf += chunk;
        if (onOutput)
            onOutput(chunk);
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, outputBuf, onDone](int exitCode, QProcess::ExitStatus status) {
        const bool ok = (status == QProcess::NormalExit && exitCode == 0);
        onDone(ok, *outputBuf);
        if (m_proc == proc) {
            m_proc = nullptr;
            proc->deleteLater();
        }
    });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning)
                proc->kill();
        });
    }

    proc->start(program, args);

    if (feedPassword) {
        proc->write(m_password);
        proc->write("\n");
        proc->closeWriteChannel();
    }
}

// ── mock install pipeline ────────────────────────────────────────────

void UpdateModel::mockRunInstallStages(int stageIndex)
{
    if (stageIndex >= kMockInstallStages.size()) {
        zeroPassword();
        setState(InstallSucceeded);
        return;
    }

    m_installStage = kMockInstallStages[stageIndex];
    m_installStagesSeen = stageIndex + 1;
    m_installProgress = double(m_installStagesSeen) / kMockInstallStages.size();
    emit installProgressChanged();

    QTimer::singleShot(1500, this, [this, stageIndex]() {
        mockRunInstallStages(stageIndex + 1);
    });
}

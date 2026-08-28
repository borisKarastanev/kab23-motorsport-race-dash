#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

// Fire-and-forget subprocess runner shared by the models that shell out to a
// system CLI (NetworkModel -> nmcli, TimeModel -> timedatectl).
//
// Extracted because the two had byte-identical copies of this: the same
// MergedChannels setup, the same shared output buffer, the same double-fire
// guard, the same kill-on-timeout, the same deleteLater ordering. That is
// subtle process-lifetime logic, and two copies means a fix to either one
// silently leaves the other wrong.
//
// UpdateModel::startProcessStep deliberately does NOT use this: it tracks a
// single m_proc across a state machine, feeds a sudo password on stdin, and
// streams output for progress parsing. Those differences are its whole point,
// so folding it in here would mean parameterizing all three into this.
namespace Proc {

// Runs `program args`, calling onDone(ok, exitCode, mergedOutput) exactly once.
// `owner` scopes every connection and parents the QProcess, so a destroyed
// owner tears the call down. timeoutMs <= 0 disables the kill timer.
inline void run(QObject *owner, const QString &program, const QStringList &args, int timeoutMs,
                std::function<void(bool ok, int exitCode, const QString &out)> onDone)
{
    QProcess *proc = new QProcess(owner);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    auto outputBuf = std::make_shared<QString>();
    auto doneFlag = std::make_shared<bool>(false);

    QObject::connect(proc, &QProcess::readyReadStandardOutput, owner, [proc, outputBuf]() {
        *outputBuf += QString::fromUtf8(proc->readAllStandardOutput());
    });

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner,
        [proc, outputBuf, doneFlag, onDone](int exitCode, QProcess::ExitStatus status) {
            if (*doneFlag)
                return;
            *doneFlag = true;
            const bool ok = (status == QProcess::NormalExit && exitCode == 0);
            onDone(ok, exitCode, *outputBuf);
            proc->deleteLater();
        });

    QObject::connect(proc, &QProcess::errorOccurred, owner,
        [proc, doneFlag, onDone, program](QProcess::ProcessError err) {
            if (err != QProcess::FailedToStart || *doneFlag)
                return;
            *doneFlag = true;
            onDone(false, -1, program + QStringLiteral(" not found or failed to start"));
            proc->deleteLater();
        });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning)
                proc->kill();
        });
    }

    proc->start(program, args);
}

// Tail of a subprocess's output — what these models put in front of the user
// when a call fails, so the trimming rule stays the same on every page.
inline QString lastNonEmptyLine(const QString &text)
{
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

} // namespace Proc

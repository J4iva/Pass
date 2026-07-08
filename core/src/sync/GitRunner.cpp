// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/GitRunner.h"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace pass::sync {

namespace {

// Entorno endurecido: nunca prompt por terminal (colgaría) y sin pager. Se parte
// del entorno del sistema para conservar PATH y la config de Git Credential Manager.
QProcessEnvironment hardenedEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    return env;
}

// owner/repo: alfanumérico, punto, guion y guion bajo.
const QRegularExpression& ownerRepoPath() {
    static const QRegularExpression re(
        QStringLiteral("^/[A-Za-z0-9._-]+/[A-Za-z0-9._-]+?(?:\\.git)?/?$"));
    return re;
}

const QRegularExpression& scpLikeGithub() {
    static const QRegularExpression re(
        QStringLiteral("^git@github\\.com:[A-Za-z0-9._-]+/[A-Za-z0-9._-]+(?:\\.git)?$"));
    return re;
}

} // namespace

GitRunner::GitRunner(QString repoDir, QObject* parent)
    : QObject(parent), m_repoDir(std::move(repoDir)) {}

bool GitRunner::isAllowedRemoteUrl(const QString& url, bool allowLocalRemotes) {
    const QString u = url.trimmed();
    if (u.isEmpty())
        return false;

    // SSH estilo scp: git@github.com:owner/repo(.git). No lleva credenciales.
    if (scpLikeGithub().match(u).hasMatch())
        return true;

    const QUrl parsed(u, QUrl::StrictMode);
    if (parsed.isValid() && parsed.scheme() == QStringLiteral("https")) {
        // Credenciales embebidas (https://user:token@host) prohibidas.
        if (!parsed.userInfo().isEmpty())
            return false;
        if (parsed.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) != 0)
            return false;
        if (parsed.port() != -1)
            return false;
        if (!parsed.query().isEmpty() || !parsed.fragment().isEmpty())
            return false;
        return ownerRepoPath().match(parsed.path()).hasMatch();
    }

    // Solo para tests: rutas/file:// locales (repo bare en QTemporaryDir).
    if (allowLocalRemotes) {
        if (parsed.isValid() && parsed.scheme() == QStringLiteral("file"))
            return true;
        if (!u.contains(QStringLiteral("://")) && QFileInfo(u).isAbsolute())
            return true;
    }
    return false;
}

QString GitRunner::redacted(const QString& text) {
    // esquema://usuario[:password]@  ->  esquema://***@
    static const QRegularExpression re(QStringLiteral("([A-Za-z][A-Za-z0-9+.-]*://)[^/@\\s]+@"));
    QString out = text;
    out.replace(re, QStringLiteral("\\1***@"));
    return out;
}

void GitRunner::run(const QStringList& args, Callback cb, int timeoutMs) {
    auto* proc = new QProcess(this);
    proc->setWorkingDirectory(m_repoDir);
    proc->setProcessEnvironment(hardenedEnv());

    auto* timer = new QTimer(proc); // muere con el proceso
    timer->setSingleShot(true);

    // Estado compartido: garantiza un único callback y recuerda si hubo timeout.
    struct State {
        bool done = false;
        bool timedOut = false;
    };
    auto state = std::make_shared<State>();

    auto finish = [proc, timer, cb, state](bool started) {
        if (state->done)
            return;
        state->done = true;
        timer->stop();
        Result r;
        r.started = started;
        r.timedOut = state->timedOut;
        if (started) {
            r.exitCode = proc->exitCode();
            r.ok = !state->timedOut && proc->exitStatus() == QProcess::NormalExit &&
                   r.exitCode == 0;
            r.stdOut = QString::fromUtf8(proc->readAllStandardOutput());
            r.stdErr = redacted(QString::fromUtf8(proc->readAllStandardError()));
        }
        cb(r);
        proc->deleteLater();
    };

    connect(timer, &QTimer::timeout, proc, [proc, state]() {
        state->timedOut = true;
        proc->kill();
    });
    // Conexiones en cola: el callback se entrega SIEMPRE de forma asíncrona, nunca
    // de forma reentrante dentro de run(). En Windows QProcess puede emitir
    // errorOccurred(FailedToStart) de forma síncrona durante start(); sin la cola,
    // el callback correría antes de que el llamante volviera al bucle de eventos.
    connect(
        proc, &QProcess::errorOccurred, this,
        [finish](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                finish(/*started=*/false);
        },
        Qt::QueuedConnection);
    connect(
        proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [finish](int, QProcess::ExitStatus) { finish(/*started=*/true); }, Qt::QueuedConnection);

    timer->start(timeoutMs);
    proc->start(m_git, args);
}

GitRunner::Result GitRunner::runBlocking(const QStringList& args, int timeoutMs) {
    Result r;
    QProcess proc;
    proc.setWorkingDirectory(m_repoDir);
    proc.setProcessEnvironment(hardenedEnv());
    proc.start(m_git, args);

    if (!proc.waitForStarted(timeoutMs)) {
        r.started = false;
        return r;
    }
    r.started = true;
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        r.timedOut = true;
        r.stdErr = redacted(QString::fromUtf8(proc.readAllStandardError()));
        return r;
    }
    r.exitCode = proc.exitCode();
    r.ok = proc.exitStatus() == QProcess::NormalExit && r.exitCode == 0;
    r.stdOut = QString::fromUtf8(proc.readAllStandardOutput());
    r.stdErr = redacted(QString::fromUtf8(proc.readAllStandardError()));
    return r;
}

} // namespace pass::sync

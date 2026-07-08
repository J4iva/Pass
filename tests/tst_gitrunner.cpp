// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/GitRunner.h"

#include <QEventLoop>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace pass::sync;

class GitRunnerTest : public QObject {
    Q_OBJECT

private slots:
    void allowsOnlyGithubRemotes() {
        // Aceptadas.
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("https://github.com/owner/repo")));
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("https://github.com/owner/repo.git")));
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("https://github.com/owner/repo/")));
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("git@github.com:owner/repo.git")));
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("git@github.com:owner/repo")));
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("https://github.com/o-w_n.er/re.po")));
    }

    void rejectsHostileRemotes() {
        const QStringList hostile = {
            QString(),
            QStringLiteral("https://user:token@github.com/owner/repo.git"), // credenciales
            QStringLiteral("https://github.com/owner/repo/extra"),          // demasiados segmentos
            QStringLiteral("https://github.com/onlyowner"),                 // falta repo
            QStringLiteral("https://gitlab.com/owner/repo"),                // otro host
            QStringLiteral("http://github.com/owner/repo"),                 // sin TLS
            QStringLiteral("https://github.com:8443/owner/repo"),           // puerto
            QStringLiteral("ssh://github.com/owner/repo"),                  // ssh arbitrario
            QStringLiteral("file:///tmp/repo"),                             // local
            QStringLiteral("git@gitlab.com:owner/repo.git"),                // otro host (scp)
            QStringLiteral("https://github.com/owner/repo;rm -rf"),         // metacaracteres
        };
        for (const QString& u : hostile)
            QVERIFY2(!GitRunner::isAllowedRemoteUrl(u), qPrintable(u));
    }

    void allowsLocalRemotesOnlyWhenOptedIn() {
        const QString local = QStringLiteral("file:///tmp/repo");
        QVERIFY(!GitRunner::isAllowedRemoteUrl(local, /*allowLocalRemotes=*/false));
        QVERIFY(GitRunner::isAllowedRemoteUrl(local, /*allowLocalRemotes=*/true));
        QVERIFY(GitRunner::isAllowedRemoteUrl(QStringLiteral("C:/Users/x/repo"),
                                              /*allowLocalRemotes=*/true));
    }

    void redactsEmbeddedCredentials() {
        const QString in = QStringLiteral(
            "fatal: Authentication failed for 'https://alice:s3cr3t@github.com/o/r.git'");
        const QString out = GitRunner::redacted(in);
        QVERIFY(!out.contains(QStringLiteral("s3cr3t")));
        QVERIFY(!out.contains(QStringLiteral("alice")));
        QVERIFY(out.contains(QStringLiteral("***@github.com")));
        // git@host (SSH) no es una credencial y no se toca.
        QCOMPARE(GitRunner::redacted(QStringLiteral("git@github.com:o/r.git")),
                 QStringLiteral("git@github.com:o/r.git"));
    }

    void reportsFailureWhenGitMissing() {
        QTemporaryDir dir;
        GitRunner runner(dir.path());
        runner.setGitProgram(QStringLiteral("programa-que-no-existe-pass-xyz"));
        GitRunner::Result res;
        QEventLoop loop;
        runner.run({QStringLiteral("status")}, [&](const GitRunner::Result& r) {
            res = r;
            loop.quit();
        });
        loop.exec();
        QVERIFY(!res.started);
        QVERIFY(!res.ok);
    }

    void killsOnTimeout() {
        const QString ping = QStandardPaths::findExecutable(QStringLiteral("ping"));
        if (ping.isEmpty())
            QSKIP("ping no disponible para simular un proceso lento");
        QTemporaryDir dir;
        GitRunner runner(dir.path());
        runner.setGitProgram(ping);
        // ping bloquea ~4 s; con timeout de 300 ms debe matarse y marcar timedOut.
        const auto r = runner.runBlocking(
            {QStringLiteral("-n"), QStringLiteral("5"), QStringLiteral("127.0.0.1")}, 300);
        QVERIFY(r.started);
        QVERIFY(r.timedOut);
        QVERIFY(!r.ok);
    }

    void runsSuccessfully() {
        const QString ping = QStandardPaths::findExecutable(QStringLiteral("ping"));
        if (ping.isEmpty())
            QSKIP("ping no disponible para probar una ejecución correcta");
        QTemporaryDir dir;
        GitRunner runner(dir.path());
        runner.setGitProgram(ping);
        GitRunner::Result res;
        QEventLoop loop;
        runner.run({QStringLiteral("-n"), QStringLiteral("1"), QStringLiteral("127.0.0.1")},
                   [&](const GitRunner::Result& r) {
                       res = r;
                       loop.quit();
                   },
                   5000);
        loop.exec();
        QVERIFY(res.started);
        QVERIFY(res.ok);
    }
};

QTEST_GUILESS_MAIN(GitRunnerTest)
#include "tst_gitrunner.moc"

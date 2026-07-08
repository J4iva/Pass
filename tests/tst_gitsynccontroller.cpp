// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test de integración del sync entre dispositivos a través de GitSyncController
// (pipeline en un hilo worker con su PROPIA conexión SQLite). Verifica que:
//   - Un dato escrito en la conexión del hilo GUI se exporta y sube desde el hilo
//     worker (vía su 2ª conexión al MISMO fichero, en WAL) sin congelar al test.
//   - Otro dispositivo lo recibe (round-trip por el repo).
//   - shutdownSync() cierra de forma acotada sin bloquearse (sin deadlock).
//
// Nota: el dispositivo A usa un fichero .db REAL (no ":memory:") porque las dos
// conexiones (GUI + worker) deben compartir el mismo fichero; con ":memory:" cada
// conexión sería una base distinta.
#include "pass/db/Database.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/sync/GitSyncController.h"
#include "pass/sync/GitSyncService.h"

#include <QDir>
#include <QEventLoop>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace pass;
using namespace pass::sync;

namespace {

QString gitExe() {
    return QStandardPaths::findExecutable(QStringLiteral("git"));
}

bool runGit(const QString& cwd, const QStringList& args) {
    QProcess p;
    p.setWorkingDirectory(cwd);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("t@local"));
    env.insert(QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("t@local"));
    p.setProcessEnvironment(env);
    p.start(gitExe(), args);
    return p.waitForStarted(10000) && p.waitForFinished(60000) &&
           p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// El controller es asíncrono: SIEMPRE corremos el bucle de eventos y esperamos a
// un estado terminal (la señal llega en cola desde el hilo worker).
void driveSyncController(GitSyncController& svc) {
    QEventLoop loop;
    QObject ctx;
    QObject::connect(&svc, &GitSyncController::statusChanged, &ctx,
                     [&](GitSyncService::Status st) {
                         if (st == GitSyncService::Status::Idle ||
                             st == GitSyncService::Status::Warning ||
                             st == GitSyncService::Status::Error)
                             loop.quit();
                     });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    svc.syncNow();
    watchdog.start(60000);
    loop.exec();
}

// Dispositivo plano (síncrono) para el otro extremo del round-trip.
void driveSyncService(GitSyncService& svc) {
    QEventLoop loop;
    QObject ctx;
    QObject::connect(&svc, &GitSyncService::statusChanged, &ctx, [&](GitSyncService::Status st) {
        if (st != GitSyncService::Status::Syncing)
            loop.quit();
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    svc.syncNow();
    if (svc.status() == GitSyncService::Status::Syncing) {
        watchdog.start(60000);
        loop.exec();
    }
}

} // namespace

class GitSyncControllerTest : public QObject {
    Q_OBJECT

private:
    QString m_base;
    QString m_remote;

    void initRemote() {
        QVERIFY(runGit(m_base, {QStringLiteral("init"), QStringLiteral("--bare"),
                                QStringLiteral("-b"), QStringLiteral("main"), m_remote}));
    }
    QString clone(const QString& name) {
        const QString dir = m_base + QLatin1Char('/') + name;
        const bool ok = runGit(m_base, {QStringLiteral("clone"), m_remote, dir});
        return ok ? dir : QString();
    }

private slots:
    void init() {
        if (gitExe().isEmpty())
            QSKIP("git no disponible en el sistema");
    }

    void pushesFromWorkerThreadAndPropagates() {
        QTemporaryDir base;
        m_base = base.path();
        m_remote = m_base + QStringLiteral("/remote.git");
        initRemote();
        const QString cloneA = clone(QStringLiteral("A"));
        QVERIFY(!cloneA.isEmpty());

        // Dispositivo A a través del CONTROLLER (hilo worker + 2ª conexión).
        const QString dbPathA = m_base + QStringLiteral("/A.db");
        Database dbA(dbPathA);
        QVERIFY(dbA.isOpen());
        GitSyncController ctrlA(dbA.handle(), dbPathA);
        ctrlA.setAllowLocalRemotes(true);
        ctrlA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        ctrlA.setRepo(cloneA, QStringLiteral("main"));

        // El dato se escribe en la conexión del hilo GUI...
        SubjectRepository subjectsA(dbA.handle());
        Subject mates{QUuid::createUuid(), QStringLiteral("Mates"), QStringLiteral("#112233"),
                      false, {}};
        QVERIFY(subjectsA.add(mates));

        // ...y el worker (en su hilo, con su propia conexión) lo exporta y sube.
        driveSyncController(ctrlA);
        QCOMPARE(ctrlA.status(), GitSyncService::Status::Idle);

        // Otro dispositivo lo recibe (round-trip por el repo).
        const QString cloneB = clone(QStringLiteral("B"));
        QVERIFY(!cloneB.isEmpty());
        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        driveSyncService(svcB);
        QCOMPARE(svcB.status(), GitSyncService::Status::Idle);

        SubjectRepository subjectsB(dbB.handle());
        QCOMPARE(subjectsB.all().size(), 1);
        QCOMPARE(subjectsB.all().first().name, QStringLiteral("Mates"));

        // Cierre acotado: no debe bloquearse (si hubiera deadlock, el test colgaría).
        ctrlA.shutdownSync(5000);
    }

    void shutdownWithoutRepoDoesNotHang() {
        // Sin repositorio configurado: shutdown debe cerrar el hilo limpiamente.
        QTemporaryDir base;
        const QString dbPath = base.path() + QStringLiteral("/x.db");
        Database db(dbPath);
        QVERIFY(db.isOpen());
        GitSyncController ctrl(db.handle(), dbPath);
        ctrl.shutdownSync(2000); // sin repo: ni push ni bloqueo
        QVERIFY(!ctrl.isConfigured());
    }
};

QTEST_GUILESS_MAIN(GitSyncControllerTest)
#include "tst_gitsynccontroller.moc"

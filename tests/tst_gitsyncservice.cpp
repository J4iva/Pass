// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Database.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/sync/GitSyncService.h"

#include <QDir>
#include <QEventLoop>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlQuery>
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
    // Identidad por si el git global no la tiene (CI/clon nuevo).
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("t@local"));
    env.insert(QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("t@local"));
    p.setProcessEnvironment(env);
    p.start(gitExe(), args);
    return p.waitForStarted(10000) && p.waitForFinished(60000) &&
           p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// Lanza un ciclo completo de sincronización y espera (con watchdog) a que el
// estado deje de ser Syncing.
void driveSync(GitSyncService& svc) {
    QEventLoop loop;
    QObject ctx;
    QObject::connect(&svc, &GitSyncService::statusChanged, &ctx,
                     [&](GitSyncService::Status st) {
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

void setSubjectViaSql(QSqlDatabase db, const QString& id, const QString& name,
                      const QString& updatedAt) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE subjects SET name = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(name);
    q.addBindValue(updatedAt);
    q.addBindValue(id);
    q.exec();
}

QString subjectName(QSqlDatabase db, const QString& id) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT name FROM subjects WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return QStringLiteral("<none>");
    return q.value(0).toString();
}

} // namespace

class GitSyncServiceTest : public QObject {
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

    void propagatesNewSubjectBetweenDevices() {
        QTemporaryDir base;
        m_base = base.path();
        m_remote = m_base + QStringLiteral("/remote.git");
        initRemote();
        const QString cloneA = clone(QStringLiteral("A"));
        const QString cloneB = clone(QStringLiteral("B"));
        QVERIFY(!cloneA.isEmpty() && !cloneB.isEmpty());

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));

        SubjectRepository subjectsA(dbA.handle());
        Subject mates{QUuid::createUuid(), QStringLiteral("Mates"), QStringLiteral("#112233"), false,
                      {}};
        QVERIFY(subjectsA.add(mates));
        driveSync(svcA);
        QCOMPARE(svcA.status(), GitSyncService::Status::Idle);

        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        driveSync(svcB);
        QCOMPARE(svcB.status(), GitSyncService::Status::Idle);

        SubjectRepository subjectsB(dbB.handle());
        QCOMPARE(subjectsB.all().size(), 1);
        QCOMPARE(subjectsB.all().first().name, QStringLiteral("Mates"));
    }

    void propagatesTombstone() {
        QTemporaryDir base;
        m_base = base.path();
        m_remote = m_base + QStringLiteral("/remote.git");
        initRemote();
        const QString cloneA = clone(QStringLiteral("A"));
        QVERIFY(!cloneA.isEmpty());

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        SubjectRepository subjectsA(dbA.handle());
        Subject s{QUuid::createUuid(), QStringLiteral("Borrame"), {}, false, {}};
        QVERIFY(subjectsA.add(s));
        driveSync(svcA);

        // B se une y recibe la asignatura.
        const QString cloneB = clone(QStringLiteral("B"));
        QVERIFY(!cloneB.isEmpty());
        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        driveSync(svcB);
        SubjectRepository subjectsB(dbB.handle());
        QCOMPARE(subjectsB.all().size(), 1);

        // A borra y sincroniza; B sincroniza y debe quedarse sin la asignatura.
        QVERIFY(subjectsA.remove(s.id));
        driveSync(svcA);
        driveSync(svcB);
        QCOMPARE(subjectsB.all().size(), 0);
    }

    void concurrentDifferentRecordsConverge() {
        QTemporaryDir base;
        m_base = base.path();
        m_remote = m_base + QStringLiteral("/remote.git");
        initRemote();
        const QString cloneA = clone(QStringLiteral("A"));
        QVERIFY(!cloneA.isEmpty());

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        SubjectRepository subjectsA(dbA.handle());
        Subject s1{QUuid::createUuid(), QStringLiteral("Uno"), {}, false, {}};
        Subject s2{QUuid::createUuid(), QStringLiteral("Dos"), {}, false, {}};
        QVERIFY(subjectsA.add(s1));
        QVERIFY(subjectsA.add(s2));
        driveSync(svcA);

        const QString cloneB = clone(QStringLiteral("B"));
        QVERIFY(!cloneB.isEmpty());
        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        driveSync(svcB);

        const QString id1 = s1.id.toString(QUuid::WithoutBraces);
        const QString id2 = s2.id.toString(QUuid::WithoutBraces);
        // Ediciones concurrentes en registros distintos, con fechas controladas.
        setSubjectViaSql(dbA.handle(), id1, QStringLiteral("Uno-A"),
                         QStringLiteral("2027-05-01T00:00:00Z"));
        setSubjectViaSql(dbB.handle(), id2, QStringLiteral("Dos-B"),
                         QStringLiteral("2027-05-01T00:00:00Z"));

        driveSync(svcA);             // A sube Uno-A
        driveSync(svcB);             // B sube Dos-B (carrera: fetch+merge+repush)
        driveSync(svcA);             // A baja Dos-B

        QCOMPARE(subjectName(dbA.handle(), id1), QStringLiteral("Uno-A"));
        QCOMPARE(subjectName(dbA.handle(), id2), QStringLiteral("Dos-B"));
        QCOMPARE(subjectName(dbB.handle(), id1), QStringLiteral("Uno-A"));
        QCOMPARE(subjectName(dbB.handle(), id2), QStringLiteral("Dos-B"));
    }

    void notesSyncFromExternalVault() {
        // Las notas de Pass viven en un vault FUERA del repo; deben espejarse al
        // repo y propagarse al vault del otro dispositivo.
        QTemporaryDir base;
        m_base = base.path();
        m_remote = m_base + QStringLiteral("/remote.git");
        initRemote();
        const QString cloneA = clone(QStringLiteral("A"));
        QVERIFY(!cloneA.isEmpty());

        const QString vaultA = m_base + QStringLiteral("/vaultA/Pass");
        QVERIFY(QDir().mkpath(vaultA));
        {
            QFile f(vaultA + QStringLiteral("/nota.md"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("apuntes de A");
        }

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        svcA.setNotesDir(vaultA);
        driveSync(svcA);
        QCOMPARE(svcA.status(), GitSyncService::Status::Idle);
        // La nota llegó al notes/ del repo (clon A).
        QVERIFY(QFile::exists(cloneA + QStringLiteral("/notes/nota.md")));

        // Dispositivo B con un vault propio (vacío) en otra ruta.
        const QString cloneB = clone(QStringLiteral("B"));
        QVERIFY(!cloneB.isEmpty());
        const QString vaultB = m_base + QStringLiteral("/vaultB/Pass");
        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        svcB.setNotesDir(vaultB);
        driveSync(svcB);

        // El vault de B recibió la nota de A aunque su vault esté en otra carpeta.
        QFile note(vaultB + QStringLiteral("/nota.md"));
        QVERIFY(note.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(note.readAll()), QStringLiteral("apuntes de A"));
    }

    void noteConflictKeepsBothVersions() {
        QTemporaryDir base;
        m_base = base.path();
        m_remote = m_base + QStringLiteral("/remote.git");
        initRemote();
        const QString cloneA = clone(QStringLiteral("A"));
        const QString cloneB = clone(QStringLiteral("B"));
        QVERIFY(!cloneA.isEmpty() && !cloneB.isEmpty());

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));

        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));

        auto writeNote = [](const QString& clone, const QString& content) {
            QDir().mkpath(clone + QStringLiteral("/notes"));
            QFile f(clone + QStringLiteral("/notes/foo.md"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content.toUtf8());
        };

        // A crea la nota y la sube.
        writeNote(cloneA, QStringLiteral("contenido de A"));
        driveSync(svcA);

        // B crea la MISMA nota con otro contenido y sincroniza -> conflicto.
        writeNote(cloneB, QStringLiteral("contenido de B"));
        driveSync(svcB);

        // Gana el remoto (A) en foo.md; la versión de B se conserva como copia.
        QFile note(cloneB + QStringLiteral("/notes/foo.md"));
        QVERIFY(note.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(note.readAll()), QStringLiteral("contenido de A"));

        const QStringList copies =
            QDir(cloneB + QStringLiteral("/notes"))
                .entryList({QStringLiteral("foo (conflicto*")}, QDir::Files);
        QVERIFY(!copies.isEmpty());
    }
};

QTEST_GUILESS_MAIN(GitSyncServiceTest)
#include "tst_gitsyncservice.moc"

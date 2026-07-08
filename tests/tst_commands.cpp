// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests E2E del CLI por comando: un `command/*.passcmd` escrito en el repo de
// sync se procesa al sincronizar (y al arrancar), crea el recurso, se mueve a
// `command/processed/` y se propaga al otro dispositivo por el pipeline normal.
#include "pass/db/Database.h"
#include "pass/notes/NoteSerializer.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/sync/GitSyncService.h"

#include <QDate>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
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

// Escribe un fichero (creando la carpeta). Para inyectar un comando en el repo.
bool writeFile(const QString& path, const QString& content) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&f) << content;
    return f.commit();
}

// Lee cuántos .md hay bajo `dir` (no recursivo) cuyo frontmatter satisface un
// predicado. Para localizar la nota creada por comando sin depender de su nombre
// (que lleva timestamp).
int countNotesMatching(const QString& dir, const QString& key, const QString& value) {
    QDir d(dir);
    if (!d.exists())
        return 0;
    int n = 0;
    const auto entries = d.entryInfoList({QStringLiteral("*.md")}, QDir::Files);
    for (const QFileInfo& fi : entries) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const auto doc = NoteSerializer::parse(QString::fromUtf8(f.readAll()));
        if (NoteSerializer::value(doc, key) == value)
            ++n;
    }
    return n;
}

} // namespace

class CommandsTest : public QObject {
    Q_OBJECT

private:
    void initRemote(const QString& base, const QString& remote) {
        QVERIFY(runGit(base, {QStringLiteral("init"), QStringLiteral("--bare"),
                              QStringLiteral("-b"), QStringLiteral("main"), remote}));
    }
    QString clone(const QString& base, const QString& name, const QString& remote) {
        const QString dir = base + QLatin1Char('/') + name;
        return runGit(base, {QStringLiteral("clone"), remote, dir}) ? dir : QString();
    }

private slots:
    void init() {
        if (gitExe().isEmpty())
            QSKIP("git no disponible en el sistema");
    }

    void createSubjectPropagatesAndMovesToProcessed() {
        QTemporaryDir base;
        const QString remote = base.path() + QStringLiteral("/remote.git");
        initRemote(base.path(), remote);
        const QString cloneA = clone(base.path(), QStringLiteral("A"), remote);
        const QString cloneB = clone(base.path(), QStringLiteral("B"), remote);
        QVERIFY(!cloneA.isEmpty() && !cloneB.isEmpty());

        // Inyecta un comando en el repo de A.
        QVERIFY(writeFile(cloneA + QStringLiteral("/command/create-subject.passcmd"),
                          QStringLiteral("Pass create subject Cálculo")));

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        driveSync(svcA);
        QCOMPARE(svcA.status(), GitSyncService::Status::Idle);

        // A: el subject se creó en su BD y el comando se movió a processed/.
        SubjectRepository subjectsA(dbA.handle());
        QCOMPARE(subjectsA.all().size(), 1);
        QCOMPARE(subjectsA.all().first().name, QStringLiteral("Cálculo"));
        QVERIFY(!QFile::exists(cloneA + QStringLiteral("/command/create-subject.passcmd")));
        QVERIFY(QFile::exists(cloneA + QStringLiteral("/command/processed/create-subject.passcmd")));

        // B sincroniza: importa el subject y NO reprocesa el comando (ya procesado).
        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        driveSync(svcB);
        QCOMPARE(svcB.status(), GitSyncService::Status::Idle);

        SubjectRepository subjectsB(dbB.handle());
        QCOMPARE(subjectsB.all().size(), 1);
        QCOMPARE(subjectsB.all().first().name, QStringLiteral("Cálculo"));
    }

    void twoCommandsInOneCycleCreateSubjectThenTask() {
        QTemporaryDir base;
        const QString remote = base.path() + QStringLiteral("/remote.git");
        initRemote(base.path(), remote);
        const QString cloneA = clone(base.path(), QStringLiteral("A"), remote);
        QVERIFY(!cloneA.isEmpty());

        QVERIFY(writeFile(cloneA + QStringLiteral("/command/01-subject.passcmd"),
                          QStringLiteral("Pass create subject Física")));
        QVERIFY(writeFile(cloneA + QStringLiteral("/command/02-task.passcmd"),
                          QStringLiteral("Pass create task \"Lab 1\" --due 2026-07-20T22:00:00Z --subject Física")));

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        driveSync(svcA);
        QCOMPARE(svcA.status(), GitSyncService::Status::Idle);

        // Ambos comandos procesados (en processed/), subject y tarea creados.
        QCOMPARE(SubjectRepository(dbA.handle()).all().size(), 1);
        QCOMPARE(EventRepository(dbA.handle())
                     .between(QDateTime(QDate(2026, 7, 1), QTime(0, 0)),
                              QDateTime(QDate(2026, 12, 31), QTime(0, 0)))
                     .size(),
                 1);
        QVERIFY(QFile::exists(cloneA + QStringLiteral("/command/processed/01-subject.passcmd")));
        QVERIFY(QFile::exists(cloneA + QStringLiteral("/command/processed/02-task.passcmd")));
    }

    void createNoteReachesVaultOnBothDevices() {
        QTemporaryDir base;
        QTemporaryDir vaultA;
        QTemporaryDir vaultB;
        const QString remote = base.path() + QStringLiteral("/remote.git");
        initRemote(base.path(), remote);
        const QString cloneA = clone(base.path(), QStringLiteral("A"), remote);
        const QString cloneB = clone(base.path(), QStringLiteral("B"), remote);
        QVERIFY(!cloneA.isEmpty() && !cloneB.isEmpty());

        Database dbA(QStringLiteral(":memory:"));
        SubjectRepository(dbA.handle()).add(
            {QUuid::createUuid(), QStringLiteral("Cálculo"), QString(), false, {}});
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        svcA.setNotesDir(vaultA.path());

        // Un primer sync publica el subject preexistente de A.
        driveSync(svcA);

        // Ahora inyecta el comando de nota y vuelve a sincronizar.
        QVERIFY(writeFile(cloneA + QStringLiteral("/command/note.passcmd"),
                          QStringLiteral("Pass create note Integrales --subject Cálculo --body \"resumen\"")));
        driveSync(svcA);
        QCOMPARE(svcA.status(), GitSyncService::Status::Idle);

        // B sincroniza con su vault: debe recibir la nota.
        Database dbB(QStringLiteral(":memory:"));
        GitSyncService svcB(dbB.handle());
        svcB.setAllowLocalRemotes(true);
        svcB.setIdentity(QStringLiteral("devB"), QStringLiteral("Equipo B"));
        svcB.setRepo(cloneB, QStringLiteral("main"));
        svcB.setNotesDir(vaultB.path());
        driveSync(svcB);
        QCOMPARE(svcB.status(), GitSyncService::Status::Idle);

        QCOMPARE(countNotesMatching(vaultB.path(), QStringLiteral("subject"), QStringLiteral("Cálculo")), 1);
    }

    void malformedCommandStaysInPlaceAndDoesNotBreakCycle() {
        QTemporaryDir base;
        const QString remote = base.path() + QStringLiteral("/remote.git");
        initRemote(base.path(), remote);
        const QString cloneA = clone(base.path(), QStringLiteral("A"), remote);
        QVERIFY(!cloneA.isEmpty());

        QVERIFY(writeFile(cloneA + QStringLiteral("/command/bad.passcmd"),
                          QStringLiteral("Pass create nonexistententity foo")));

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        driveSync(svcA);
        // El ciclo termina bien (Idle o Warning); el comando sigue en sitio.
        QVERIFY(svcA.status() == GitSyncService::Status::Idle ||
                svcA.status() == GitSyncService::Status::Warning);
        QVERIFY(QFile::exists(cloneA + QStringLiteral("/command/bad.passcmd")));
        QVERIFY(!QFile::exists(cloneA + QStringLiteral("/command/processed/bad.passcmd")));
    }

    void commandsDisabledSkipsProcessing() {
        QTemporaryDir base;
        const QString remote = base.path() + QStringLiteral("/remote.git");
        initRemote(base.path(), remote);
        const QString cloneA = clone(base.path(), QStringLiteral("A"), remote);
        QVERIFY(!cloneA.isEmpty());

        QVERIFY(writeFile(cloneA + QStringLiteral("/command/create-subject.passcmd"),
                          QStringLiteral("Pass create subject Cálculo")));

        Database dbA(QStringLiteral(":memory:"));
        GitSyncService svcA(dbA.handle());
        svcA.setAllowLocalRemotes(true);
        svcA.setIdentity(QStringLiteral("devA"), QStringLiteral("Equipo A"));
        svcA.setRepo(cloneA, QStringLiteral("main"));
        svcA.setCommandsEnabled(false); // feature desactivada
        driveSync(svcA);

        // No se procesó: no hay subject y el comando sigue en command/.
        QCOMPARE(SubjectRepository(dbA.handle()).all().size(), 0);
        QVERIFY(QFile::exists(cloneA + QStringLiteral("/command/create-subject.passcmd")));
    }
};

QTEST_GUILESS_MAIN(CommandsTest)
#include "tst_commands.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Database.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"
#include "pass/sync/SyncExporter.h"
#include "pass/sync/SyncImporter.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace pass;
using namespace pass::sync;

namespace {

// --- helpers de BD (SQL directo: control fino del updated_at, sin pasar por repos) ---

void insertSubject(QSqlDatabase db, const QString& id, const QString& name, const QString& updatedAt,
                   const QString& color = QString()) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO subjects(id, name, color, archived, updated_at) VALUES(?, ?, ?, 0, ?)"));
    q.addBindValue(id);
    q.addBindValue(name);
    q.addBindValue(color);
    q.addBindValue(updatedAt);
    QVERIFY(q.exec());
}

void insertTopic(QSqlDatabase db, const QString& id, const QString& subjectId, const QString& name,
                 const QString& updatedAt) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO topics(id, subject_id, name, updated_at) VALUES(?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(subjectId);
    q.addBindValue(name);
    q.addBindValue(updatedAt);
    QVERIFY(q.exec());
}

void insertSession(QSqlDatabase db, const QString& id, const QString& subjectId,
                   const QString& updatedAt) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions(id, subject_id, status, actual_sec, updated_at) "
        "VALUES(?, ?, 'completed', 1500, ?)"));
    q.addBindValue(id);
    q.addBindValue(subjectId.isEmpty() ? QVariant() : QVariant(subjectId));
    q.addBindValue(updatedAt);
    QVERIFY(q.exec());
}

QString scalar(QSqlDatabase db, const QString& sql) {
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next())
        return QStringLiteral("<error>");
    return q.value(0).toString();
}

int count(QSqlDatabase db, const QString& sql) {
    return scalar(db, sql).toInt();
}

// --- helpers de archivos del espejo ---

void writeJson(const QString& repoDir, const QString& rel, const QJsonObject& obj) {
    const QString path = repoDir + QLatin1Char('/') + rel;
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(obj).toJson());
}

QJsonObject subjectFile(const QString& id, const QString& name, const QString& updatedAt,
                        const QString& color = QString()) {
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("color"), color},
            {QStringLiteral("archived"), false},
            {QStringLiteral("updated_at"), updatedAt}};
}

QJsonObject tombstoneFile(const QString& entity, const QString& id, const QString& deletedAt) {
    return {{QStringLiteral("entity"), entity},
            {QStringLiteral("id"), id},
            {QStringLiteral("deleted_at"), deletedAt}};
}

const QString kT1 = QStringLiteral("2026-01-01T00:00:00Z");
const QString kT2 = QStringLiteral("2026-02-01T00:00:00Z");
const QString kT3 = QStringLiteral("2026-03-01T00:00:00Z");

} // namespace

class SyncImportExportTest : public QObject {
    Q_OBJECT

private slots:
    void lwwNewerRemoteWins() {
        Database db(QStringLiteral(":memory:"));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSubject(db.handle(), id, QStringLiteral("Mates"), kT1);

        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("data/subjects/") + id + QStringLiteral(".json"),
                  subjectFile(id, QStringLiteral("Matemáticas"), kT2));

        SyncImporter imp(db.handle(), repo.path());
        const auto r = imp.importAll();
        QVERIFY(r.ok);
        QVERIFY(r.applied);
        QCOMPARE(scalar(db.handle(),
                        QStringLiteral("SELECT name FROM subjects WHERE id = '%1'").arg(id)),
                 QStringLiteral("Matemáticas"));
    }

    void lwwOlderRemoteIgnored() {
        Database db(QStringLiteral(":memory:"));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSubject(db.handle(), id, QStringLiteral("Mates"), kT2);

        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("data/subjects/") + id + QStringLiteral(".json"),
                  subjectFile(id, QStringLiteral("Viejo"), kT1));

        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(scalar(db.handle(),
                        QStringLiteral("SELECT name FROM subjects WHERE id = '%1'").arg(id)),
                 QStringLiteral("Mates")); // local más nuevo se conserva
    }

    void tombstoneDeletesOlderLocal() {
        Database db(QStringLiteral(":memory:"));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSubject(db.handle(), id, QStringLiteral("Borrame"), kT1);

        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("data/tombstones/") + id + QStringLiteral(".json"),
                  tombstoneFile(QStringLiteral("subjects"), id, kT2));

        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(count(db.handle(),
                       QStringLiteral("SELECT COUNT(*) FROM subjects WHERE id = '%1'").arg(id)),
                 0);
    }

    void tombstoneLosesToNewerLocal() {
        // Resurrección/anti-pérdida: la fila local más nueva sobrevive a la lápida.
        Database db(QStringLiteral(":memory:"));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSubject(db.handle(), id, QStringLiteral("Vivo"), kT3);

        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("data/tombstones/") + id + QStringLiteral(".json"),
                  tombstoneFile(QStringLiteral("subjects"), id, kT2));

        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(count(db.handle(),
                       QStringLiteral("SELECT COUNT(*) FROM subjects WHERE id = '%1'").arg(id)),
                 1);
    }

    void subjectNameCollisionMinUuidWinsAndRemaps() {
        // Local tiene el id MAYOR; el remoto trae el MENOR con el mismo nombre.
        // Debe ganar el menor (remoto), borrarse el local y remapearse la sesión.
        Database db(QStringLiteral(":memory:"));
        const QString bigId = QStringLiteral("ffffffff-0000-0000-0000-000000000000");
        const QString smallId = QStringLiteral("00000000-0000-0000-0000-000000000001");
        insertSubject(db.handle(), bigId, QStringLiteral("Física"), kT1);
        const QString sessId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSession(db.handle(), sessId, bigId, kT1);

        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("data/subjects/") + smallId + QStringLiteral(".json"),
                  subjectFile(smallId, QStringLiteral("Física"), kT2));

        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        // Solo sobrevive el id menor; la sesión apunta a él.
        QCOMPARE(count(db.handle(), QStringLiteral("SELECT COUNT(*) FROM subjects")), 1);
        QCOMPARE(scalar(db.handle(), QStringLiteral("SELECT id FROM subjects")), smallId);
        QCOMPARE(scalar(db.handle(),
                        QStringLiteral("SELECT subject_id FROM sessions WHERE id = '%1'").arg(sessId)),
                 smallId);
    }

    void sessionResolvesGoogleEventByExternalId() {
        Database db(QStringLiteral(":memory:"));
        // Fila espejo de Google con un UUID local arbitrario.
        const QString mirrorId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSqlQuery q(db.handle());
            q.prepare(QStringLiteral(
                "INSERT INTO events(id, provider, external_id, title, start_utc, end_utc, "
                "updated_at) VALUES(?, 'google', 'EXT-9', 'Clase', ?, ?, ?)"));
            q.addBindValue(mirrorId);
            q.addBindValue(kT1);
            q.addBindValue(kT1);
            q.addBindValue(kT1);
            QVERIFY(q.exec());
        }
        const QString sessId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QTemporaryDir repo;
        QJsonObject ev{{QStringLiteral("provider"), QStringLiteral("google")},
                       {QStringLiteral("external_id"), QStringLiteral("EXT-9")}};
        QJsonObject session{{QStringLiteral("id"), sessId},
                            {QStringLiteral("status"), QStringLiteral("completed")},
                            {QStringLiteral("actual_sec"), 600},
                            {QStringLiteral("event"), ev},
                            {QStringLiteral("updated_at"), kT2}};
        writeJson(repo.path(), QStringLiteral("data/sessions/") + sessId + QStringLiteral(".json"),
                  session);

        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(scalar(db.handle(),
                        QStringLiteral("SELECT event_id FROM sessions WHERE id = '%1'").arg(sessId)),
                 mirrorId); // resuelto al UUID local del espejo
    }

    void exportThenImportRoundTrip() {
        Database src(QStringLiteral(":memory:"));
        SubjectRepository subjects(src.handle());
        SessionRepository sessions(src.handle());

        Subject subj{QUuid::createUuid(), QStringLiteral("Lengua"), QStringLiteral("#aabbcc"), false,
                     {}};
        QVERIFY(subjects.add(subj));
        StudySession ses;
        ses.id = QUuid::createUuid();
        ses.subjectId = subj.id;
        ses.actualSeconds = 3000;
        ses.status = SessionStatus::Completed;
        QVERIFY(sessions.add(ses));

        QTemporaryDir repo;
        SyncExporter exp(src.handle(), repo.path());
        QVERIFY(exp.exportAll());
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/manifest.json")));

        Database dst(QStringLiteral(":memory:"));
        SyncImporter imp(dst.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(count(dst.handle(),
                       QStringLiteral("SELECT COUNT(*) FROM subjects WHERE name = 'Lengua'")),
                 1);
        QCOMPARE(scalar(dst.handle(),
                        QStringLiteral("SELECT actual_sec FROM sessions WHERE id = '%1'")
                            .arg(ses.id.toString(QUuid::WithoutBraces))),
                 QStringLiteral("3000"));
    }

    void exportRemovesDeletedFiles() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository subjects(db.handle());
        Subject subj{QUuid::createUuid(), QStringLiteral("Temporal"), {}, false, {}};
        QVERIFY(subjects.add(subj));

        QTemporaryDir repo;
        SyncExporter exp(db.handle(), repo.path());
        QVERIFY(exp.exportAll());
        const QString file = repo.path() + QStringLiteral("/data/subjects/") +
                             subj.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QVERIFY(QFile::exists(file));

        QVERIFY(subjects.remove(subj.id));
        QVERIFY(exp.exportAll());
        QVERIFY(!QFile::exists(file)); // la fila borrada desaparece del espejo
        // y deja su tombstone.
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/data/tombstones/") +
                              subj.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json")));
    }

    void topicExportImportRoundTrip() {
        Database src(QStringLiteral(":memory:"));
        SubjectRepository subjects(src.handle());
        TopicRepository topics(src.handle());
        Subject subj{QUuid::createUuid(), QStringLiteral("Mates"), {}, false, {}};
        QVERIFY(subjects.add(subj));
        Topic t;
        t.id = QUuid::createUuid();
        t.subjectId = subj.id;
        t.name = QStringLiteral("Integrales");
        QVERIFY(topics.add(t));

        QTemporaryDir repo;
        SyncExporter exp(src.handle(), repo.path());
        QVERIFY(exp.exportAll());
        QVERIFY(QFile::exists(repo.path() + QStringLiteral("/data/topics/") +
                              t.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json")));

        Database dst(QStringLiteral(":memory:"));
        SyncImporter imp(dst.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(scalar(dst.handle(),
                        QStringLiteral("SELECT name FROM topics WHERE id = '%1'")
                            .arg(t.id.toString(QUuid::WithoutBraces))),
                 QStringLiteral("Integrales"));
    }

    void subjectTombstoneCascadesTopics() {
        // Borrar una asignatura (lápida más nueva) arrastra sus temas locales.
        Database db(QStringLiteral(":memory:"));
        const QString sid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString tid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insertSubject(db.handle(), sid, QStringLiteral("Mates"), kT1);
        insertTopic(db.handle(), tid, sid, QStringLiteral("Integrales"), kT1);

        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("data/tombstones/") + sid + QStringLiteral(".json"),
                  tombstoneFile(QStringLiteral("subjects"), sid, kT2));

        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(count(db.handle(), QStringLiteral("SELECT COUNT(*) FROM subjects")), 0);
        QCOMPARE(count(db.handle(), QStringLiteral("SELECT COUNT(*) FROM topics")), 0);
    }

    void rejectsUnsupportedManifest() {
        Database db(QStringLiteral(":memory:"));
        QTemporaryDir repo;
        writeJson(repo.path(), QStringLiteral("manifest.json"),
                  {{QStringLiteral("format"), 99}});
        SyncImporter imp(db.handle(), repo.path());
        const auto r = imp.importAll();
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void importPathsIgnoresUnsafeAndForeignPaths() {
        Database db(QStringLiteral(":memory:"));
        SyncImporter imp(db.handle(), QStringLiteral("."));
        // Ni traversal ni rutas fuera de data/ deben procesarse (no debe explotar).
        const auto r = imp.importPaths({QStringLiteral("../../etc/passwd"),
                                        QStringLiteral("notes/secreta.md"),
                                        QStringLiteral("data/../data/subjects/x.json")});
        QVERIFY(r.ok);
        QVERIFY(!r.applied);
    }

    void ignoresFileWhoseIdMismatchesName() {
        Database db(QStringLiteral(":memory:"));
        const QString nameId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString otherId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QTemporaryDir repo;
        // Archivo nombrado <nameId>.json pero con id interno distinto: se ignora.
        writeJson(repo.path(), QStringLiteral("data/subjects/") + nameId + QStringLiteral(".json"),
                  subjectFile(otherId, QStringLiteral("Tramposo"), kT1));
        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(count(db.handle(), QStringLiteral("SELECT COUNT(*) FROM subjects")), 0);
    }

    void ignoresOversizedFile() {
        Database db(QStringLiteral(":memory:"));
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QTemporaryDir repo;
        // Un JSON válido pero > 256 KB (relleno en un campo desconocido) se ignora.
        QJsonObject big = subjectFile(id, QStringLiteral("Gordo"), kT1);
        big.insert(QStringLiteral("relleno"), QString(300 * 1024, QLatin1Char('x')));
        writeJson(repo.path(), QStringLiteral("data/subjects/") + id + QStringLiteral(".json"), big);
        SyncImporter imp(db.handle(), repo.path());
        QVERIFY(imp.importAll().ok);
        QCOMPARE(count(db.handle(), QStringLiteral("SELECT COUNT(*) FROM subjects")), 0);
    }
};

QTEST_GUILESS_MAIN(SyncImportExportTest)
#include "tst_syncimportexport.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/admin/SubjectAdminService.h"
#include "pass/db/Database.h"
#include "pass/domain/CalendarEvent.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"
#include "pass/sync/Tombstones.h"

#include <QSqlQuery>
#include <QtTest>

using namespace pass;

namespace {

// ¿Hay un tombstone (entity, id) en la BD? Comprueba la propagación de borrados.
bool hasTombstone(QSqlDatabase db, const char* entity, const QUuid& id) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM tombstones WHERE entity = ? AND id = ?"));
    q.addBindValue(QString::fromLatin1(entity));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}

} // namespace

class RepositoriesTest : public QObject {
    Q_OBJECT

private slots:
    void subjectCrud() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository repo(db.handle());

        Subject algebra{QUuid::createUuid(), QStringLiteral("Álgebra"), QStringLiteral("#3366ff"),
                        false};
        QVERIFY(repo.add(algebra));
        QCOMPARE(repo.all().size(), 1);

        auto fetched = repo.byId(algebra.id);
        QVERIFY(fetched.has_value());
        QCOMPARE(fetched->name, algebra.name);
        QCOMPARE(fetched->colorHex, algebra.colorHex);

        fetched->name = QStringLiteral("Álgebra Lineal");
        fetched->archived = true;
        QVERIFY(repo.update(*fetched));
        QCOMPARE(repo.all(/*includeArchived=*/false).size(), 0);
        QCOMPARE(repo.all(/*includeArchived=*/true).size(), 1);

        QVERIFY(repo.remove(algebra.id));
        QCOMPARE(repo.all(true).size(), 0);
    }

    void subjectNameIsUnique() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository repo(db.handle());
        QVERIFY(repo.add({QUuid::createUuid(), QStringLiteral("Física"), {}, false}));
        QVERIFY(!repo.add({QUuid::createUuid(), QStringLiteral("Física"), {}, false}));
    }

    void topicCrudAndTombstone() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository subjects(db.handle());
        TopicRepository topics(db.handle());

        Subject s{QUuid::createUuid(), QStringLiteral("Mates"), {}, false, {}};
        QVERIFY(subjects.add(s));

        Topic t;
        t.id = QUuid::createUuid();
        t.subjectId = s.id;
        t.name = QStringLiteral("Integrales");
        QVERIFY(topics.add(t));
        QCOMPARE(topics.bySubject(s.id).size(), 1);

        // UNIQUE(subject_id, name): no se puede repetir el tema en la asignatura.
        QVERIFY(!topics.add({QUuid::createUuid(), s.id, QStringLiteral("Integrales"), {}}));

        auto fetched = topics.byId(t.id);
        QVERIFY(fetched.has_value());
        fetched->name = QStringLiteral("Derivadas");
        QVERIFY(topics.update(*fetched));
        QCOMPARE(topics.byId(t.id)->name, QStringLiteral("Derivadas"));

        QVERIFY(topics.remove(t.id));
        QVERIFY(topics.bySubject(s.id).isEmpty());
        QVERIFY(hasTombstone(db.handle(), entity::kTopics, t.id));
    }

    // Borrar una asignatura: borra sus tareas y temas, conserva sesiones/eventos
    // no-tarea pero sin asignatura, y deja tombstone.
    void subjectAdminRemoveCascade() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository subjects(db.handle());
        TopicRepository topics(db.handle());
        EventRepository events(db.handle());
        SessionRepository sessions(db.handle());

        Subject s{QUuid::createUuid(), QStringLiteral("Mates"), {}, false, {}};
        QVERIFY(subjects.add(s));

        CalendarEvent task;
        task.id = QUuid::createUuid();
        task.title = QStringLiteral("[T] Entrega");
        task.startUtc = QDateTime::currentDateTimeUtc();
        task.endUtc = task.startUtc;
        task.subjectId = s.id;
        QVERIFY(events.add(task));

        CalendarEvent clase;
        clase.id = QUuid::createUuid();
        clase.title = QStringLiteral("Clase");
        clase.startUtc = QDateTime::currentDateTimeUtc();
        clase.endUtc = clase.startUtc;
        clase.subjectId = s.id;
        QVERIFY(events.add(clase));

        StudySession ses;
        ses.id = QUuid::createUuid();
        ses.subjectId = s.id;
        ses.status = SessionStatus::Completed;
        QVERIFY(sessions.add(ses));

        QVERIFY(topics.add({QUuid::createUuid(), s.id, QStringLiteral("Integrales"), {}}));
        QVERIFY(topics.add({QUuid::createUuid(), s.id, QStringLiteral("Derivadas"), {}}));

        SubjectAdminService admin(db.handle(), QString(), QString());
        const auto impact = admin.impactOf(s.id);
        QCOMPARE(impact.events, 2);
        QCOMPARE(impact.tasks, 1);
        QCOMPARE(impact.sessions, 1);
        QCOMPARE(impact.topics, 2);

        QString error;
        QVERIFY2(admin.remove(s.id, error), qPrintable(error));

        // La tarea se borró; la clase sobrevive pero sin asignatura.
        QVERIFY(!events.byId(task.id).has_value());
        const auto claseAfter = events.byId(clase.id);
        QVERIFY(claseAfter.has_value());
        QVERIFY(claseAfter->subjectId.isNull());
        // La sesión sobrevive sin asignatura.
        const auto sesAfter = sessions.byId(ses.id);
        QVERIFY(sesAfter.has_value());
        QVERIFY(sesAfter->subjectId.isNull());
        // Temas y asignatura, fuera; con tombstone.
        QVERIFY(topics.bySubject(s.id).isEmpty());
        QVERIFY(!subjects.byId(s.id).has_value());
        QVERIFY(hasTombstone(db.handle(), entity::kSubjects, s.id));
    }

    void subjectAdminRenameUpdatesSubject() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository subjects(db.handle());
        Subject s{QUuid::createUuid(), QStringLiteral("Mates"), {}, false, {}};
        QVERIFY(subjects.add(s));
        subjects.add({QUuid::createUuid(), QStringLiteral("Física"), {}, false, {}});

        SubjectAdminService admin(db.handle(), QString(), QString());
        QString error;
        // Nombre duplicado: rechazado.
        QVERIFY(!admin.rename(s.id, QStringLiteral("Física"), error));
        // Renombrado válido.
        QVERIFY2(admin.rename(s.id, QStringLiteral("Matemáticas"), error), qPrintable(error));
        QCOMPARE(subjects.byId(s.id)->name, QStringLiteral("Matemáticas"));
    }

    void builtinStrategiesAreSeeded() {
        Database db(QStringLiteral(":memory:"));
        StrategyRepository repo(db.handle());

        const auto strategies = repo.all();
        QCOMPARE(strategies.size(), 3);
        for (const auto& s : strategies)
            QVERIFY(s.builtin);
        QCOMPARE(strategies[0].workMinutes, 25); // orden: builtin primero, por work_min
        QCOMPARE(strategies[1].workMinutes, 45);
        QCOMPARE(strategies[2].workMinutes, 50);
    }

    void customStrategyCrudAndBuiltinProtection() {
        Database db(QStringLiteral(":memory:"));
        StrategyRepository repo(db.handle());

        PomodoroStrategy custom{QUuid::createUuid(), QStringLiteral("Maratón 90/15"), 90, 15, 30,
                                2, false};
        QVERIFY(repo.add(custom));
        QCOMPARE(repo.all().size(), 4);

        auto fetched = repo.byId(custom.id);
        QVERIFY(fetched.has_value());
        QCOMPARE(fetched->workMinutes, 90);

        // Las builtin no se pueden borrar; las personalizadas sí.
        const auto builtinId = repo.all().first().id;
        QVERIFY(!repo.remove(builtinId));
        QVERIFY(repo.remove(custom.id));
        QCOMPARE(repo.all().size(), 3);
    }

    void taskHelpers() {
        CalendarEvent task;
        task.title = QStringLiteral("[T] Entrega práctica 2");
        QVERIFY(isTask(task));
        QCOMPARE(taskDisplayTitle(task), QStringLiteral("Entrega práctica 2"));

        CalendarEvent plain;
        plain.title = QStringLiteral("Clase de álgebra");
        QVERIFY(!isTask(plain));
        QCOMPARE(taskDisplayTitle(plain), plain.title);
    }

    void sessionSecondsForEvent() {
        Database db(QStringLiteral(":memory:"));
        SessionRepository repo(db.handle());

        const QUuid taskId = QUuid::createUuid();
        StudySession s1;
        s1.id = QUuid::createUuid();
        s1.actualSeconds = 1500;
        s1.status = SessionStatus::Completed;
        s1.linkedEventId = taskId;
        QVERIFY(repo.add(s1));

        StudySession s2;
        s2.id = QUuid::createUuid();
        s2.actualSeconds = 600;
        s2.status = SessionStatus::Completed;
        s2.linkedEventId = taskId;
        QVERIFY(repo.add(s2));

        StudySession other; // sesión de otra cosa: no debe sumar
        other.id = QUuid::createUuid();
        other.actualSeconds = 999;
        other.status = SessionStatus::Completed;
        QVERIFY(repo.add(other));

        QCOMPARE(repo.totalSecondsForEvent(taskId), qint64(2100));
        QCOMPARE(repo.totalSecondsForEvent(QUuid::createUuid()), qint64(0));
    }

    // El progreso de reanudación viaja de ida y vuelta; ausente => -1/0 (NULL).
    void sessionResumeRoundTrip() {
        Database db(QStringLiteral(":memory:"));
        SessionRepository repo(db.handle());

        StudySession resumable;
        resumable.id = QUuid::createUuid();
        resumable.status = SessionStatus::Aborted;
        resumable.resumePhaseIndex = 3;
        resumable.resumeElapsedSec = 145;
        QVERIFY(repo.add(resumable));
        const auto got = repo.byId(resumable.id);
        QVERIFY(got.has_value());
        QCOMPARE(got->resumePhaseIndex, 3);
        QCOMPARE(got->resumeElapsedSec, 145);

        StudySession plain; // sin progreso => columnas NULL => -1/0 al leer
        plain.id = QUuid::createUuid();
        plain.status = SessionStatus::Completed;
        QVERIFY(repo.add(plain));
        const auto got2 = repo.byId(plain.id);
        QVERIFY(got2.has_value());
        QCOMPARE(got2->resumePhaseIndex, -1);
        QCOMPARE(got2->resumeElapsedSec, 0);

        // Al completarla (limpiar progreso) la columna vuelve a NULL.
        StudySession done = *got;
        done.status = SessionStatus::Completed;
        done.resumePhaseIndex = -1;
        done.resumeElapsedSec = 0;
        QVERIFY(repo.update(done));
        const auto got3 = repo.byId(resumable.id);
        QVERIFY(got3.has_value());
        QCOMPARE(got3->resumePhaseIndex, -1);
    }

    void addStampsUpdatedAt() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository repo(db.handle());
        Subject s{QUuid::createUuid(), QStringLiteral("Química"), {}, false, {}};
        QVERIFY(repo.add(s));
        const auto fetched = repo.byId(s.id);
        QVERIFY(fetched.has_value());
        QVERIFY(fetched->updatedAt.isValid());
        QCOMPARE(fetched->updatedAt.offsetFromUtc(), 0); // se almacena en UTC
    }

    void removeLeavesTombstone() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository subjects(db.handle());
        SessionRepository sessions(db.handle());

        Subject s{QUuid::createUuid(), QStringLiteral("Historia"), {}, false, {}};
        QVERIFY(subjects.add(s));
        QVERIFY(subjects.remove(s.id));
        QVERIFY(hasTombstone(db.handle(), entity::kSubjects, s.id));

        StudySession ses;
        ses.id = QUuid::createUuid();
        ses.status = SessionStatus::Completed;
        QVERIFY(sessions.add(ses));
        QVERIFY(sessions.remove(ses.id));
        QVERIFY(hasTombstone(db.handle(), entity::kSessions, ses.id));
    }
};

QTEST_GUILESS_MAIN(RepositoriesTest)
#include "tst_repositories.moc"

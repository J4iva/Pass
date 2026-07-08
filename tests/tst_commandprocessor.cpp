// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/command/CommandId.h"
#include "pass/command/CommandParser.h"
#include "pass/command/CommandProcessor.h"
#include "pass/db/Database.h"
#include "pass/domain/CalendarEvent.h"
#include "pass/domain/StudySession.h"
#include "pass/notes/NoteSerializer.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QDirIterator>
#include <QTemporaryDir>
#include <QtTest>

using namespace pass;
using namespace pass::command;

namespace {

// Parsea y devuelve la Command. Para los tests donde el parse DEBE tener éxito;
// si fallara inesperadamente, registra y devuelve una Command vacía, lo que hará
// que el QCOMPARE posterior del slot falle (sin abortar el exe con un return;
// dentro de una función que devuelve Command).
Command require(const QString& text) {
    const auto r = parse(text);
    if (!r.ok) {
        qWarning("parse inesperado falló en el test: %s", qPrintable(r.error));
        return {};
    }
    return r.command;
}

} // namespace

class CommandProcessorTest : public QObject {
    Q_OBJECT

private slots:
    void createsSubject() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const auto res = proc.process(require("Pass create subject Cálculo --color \"#3478f6\""));
        QCOMPARE(res.status, CommandStatus::Applied);

        SubjectRepository subjects(db.handle());
        QCOMPARE(subjects.all().size(), 1);
        const auto s = subjects.byName(QStringLiteral("Cálculo"));
        QVERIFY(s.has_value());
        QCOMPARE(s->colorHex, QStringLiteral("#3478f6"));
    }

    void createSubjectIsIdempotentByCommandId() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        proc.process(require("Pass create subject Cálculo"));
        // El mismo comando de nuevo => mismo UUIDv5 => Skipped, sin duplicado.
        const auto res = proc.process(require("Pass create subject Cálculo"));
        QCOMPARE(res.status, CommandStatus::Skipped);
        SubjectRepository subjects(db.handle());
        QCOMPARE(subjects.all().size(), 1);
    }

    void createSubjectSkipsIfNameAlreadyExists() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        // Primero crea "Cálculo" con un texto de comando (id A).
        proc.process(require("Pass create subject Cálculo"));
        // Un comando distinto (id B) que pide crear de nuevo "Cálculo": se reutiliza.
        const auto res = proc.process(require("Pass create subject Cálculo --color \"#ff0000\""));
        QCOMPARE(res.status, CommandStatus::Skipped);
        SubjectRepository subjects(db.handle());
        QCOMPARE(subjects.all().size(), 1); // no se duplica
    }

    void createSubjectFailsWithoutName() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const auto res = proc.process(require("Pass create subject --color \"#000000\""));
        QCOMPARE(res.status, CommandStatus::Failed);
        QVERIFY(res.message.contains(QStringLiteral("asignatura"), Qt::CaseInsensitive));
    }

    void createsTopicResolvingSubjectByName() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        proc.process(require("Pass create subject Cálculo"));
        const auto res = proc.process(require("Pass create topic Integrales --subject Cálculo"));
        QCOMPARE(res.status, CommandStatus::Applied);

        SubjectRepository subjects(db.handle());
        const auto s = subjects.byName(QStringLiteral("Cálculo"));
        QVERIFY(s.has_value());
        TopicRepository topics(db.handle());
        QCOMPARE(topics.bySubject(s->id).size(), 1);
    }

    void createTopicFailsIfSubjectMissing() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const auto res = proc.process(require("Pass create topic Integrales --subject Inexistente"));
        QCOMPARE(res.status, CommandStatus::Failed);
        QVERIFY(res.message.contains(QStringLiteral("Inexistente")));
    }

    void createTopicFailsWithoutSubjectFlag() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const auto res = proc.process(require("Pass create topic Integrales"));
        QCOMPARE(res.status, CommandStatus::Failed);
    }

    void createTopicSkipsIfAlreadyExistsByNameClean() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        proc.process(require("Pass create subject Cálculo"));
        proc.process(require("Pass create topic Integrales --subject Cálculo"));
        // Mismo comando (mismo id) => Skipped por id.
        const auto res = proc.process(require("Pass create topic Integrales --subject Cálculo"));
        QCOMPARE(res.status, CommandStatus::Skipped);
        SubjectRepository subjects(db.handle());
        const auto s = subjects.byName(QStringLiteral("Cálculo"));
        TopicRepository topics(db.handle());
        QCOMPARE(topics.bySubject(s->id).size(), 1);
    }

    void createsNoteWithSubject() {
        Database db(QStringLiteral(":memory:"));
        QTemporaryDir repo;
        CommandProcessor proc(db.handle(), repo.path());
        proc.process(require("Pass create subject Cálculo"));

        const QString text = "Pass create note Integrales --subject Cálculo --body \"resumen\"";
        const auto res = proc.process(require(text));
        QCOMPARE(res.status, CommandStatus::Applied);

        // El .md existe en <repo>/notes con el pass_command_id y el subject.
        const QUuid id = deterministicId(require(text));
        QString found;
        QDirIterator it(repo.path() + QStringLiteral("/notes"), {QStringLiteral("*.md")}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            QFile f(it.filePath());
            QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
            const auto doc = NoteSerializer::parse(QString::fromUtf8(f.readAll()));
            if (NoteSerializer::value(doc, QStringLiteral("pass_command_id")) ==
                id.toString(QUuid::WithoutBraces)) {
                found = it.fileName();
                QCOMPARE(NoteSerializer::value(doc, QStringLiteral("subject")), QStringLiteral("Cálculo"));
                QVERIFY(doc.body.contains(QStringLiteral("resumen")));
                break;
            }
        }
        QVERIFY(!found.isEmpty());
    }

    void createNoteIsIdempotent() {
        Database db(QStringLiteral(":memory:"));
        QTemporaryDir repo;
        CommandProcessor proc(db.handle(), repo.path());
        const QString text = "Pass create note \"Tema libre\"";
        QCOMPARE(proc.process(require(text)).status, CommandStatus::Applied);
        QCOMPARE(proc.process(require(text)).status, CommandStatus::Skipped);

        // Solo un .md en la carpeta.
        QDirIterator it(repo.path() + QStringLiteral("/notes"), {QStringLiteral("*.md")}, QDir::Files);
        int n = 0;
        while (it.hasNext()) {
            it.next();
            ++n;
        }
        QCOMPARE(n, 1);
    }

    void createNoteFailsIfSubjectMissing() {
        Database db(QStringLiteral(":memory:"));
        QTemporaryDir repo;
        CommandProcessor proc(db.handle(), repo.path());
        const auto res = proc.process(require("Pass create note X --subject Inexistente"));
        QCOMPARE(res.status, CommandStatus::Failed);
    }

    void createNoteWithoutSubject() {
        Database db(QStringLiteral(":memory:"));
        QTemporaryDir repo;
        CommandProcessor proc(db.handle(), repo.path());
        QCOMPARE(proc.process(require("Pass create note \"Idea rápida\"")).status,
                 CommandStatus::Applied);
    }

    void createsEvent() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const QString text =
            "Pass create event Examen --start 2026-06-20T08:00:00Z --end 2026-06-20T10:00:00Z";
        const auto res = proc.process(require(text));
        QCOMPARE(res.status, CommandStatus::Applied);

        const auto ev = EventRepository(db.handle()).byId(deterministicId(require(text)));
        QVERIFY(ev.has_value());
        QCOMPARE(ev->title, QStringLiteral("Examen"));
        QCOMPARE(ev->endUtc, ev->startUtc.addSecs(7200));

        // El id es determinista: reproducir el comando => Skipped, sin duplicado.
        QCOMPARE(proc.process(require(text)).status, CommandStatus::Skipped);
    }

    void createEventAllDayDefaultsEndToStart() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const QString text = "Pass create event \"Festivo\" --start 2026-06-20 --all-day";
        const auto res = proc.process(require(text));
        QCOMPARE(res.status, CommandStatus::Applied);

        const auto ev = EventRepository(db.handle()).byId(deterministicId(require(text)));
        QVERIFY(ev.has_value());
        QVERIFY(ev->allDay);
        // Sin --end y todo-el-día: end == start.
        QCOMPARE(ev->startUtc, ev->endUtc);
    }

    void createEventFailsWithoutStart() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        QCOMPARE(proc.process(require("Pass create event Examen")).status, CommandStatus::Failed);
    }

    void createEventResolvesSubject() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        proc.process(require("Pass create subject Cálculo"));
        const auto res = proc.process(
            require("Pass create event Examen --start 2026-06-20T08:00:00Z --subject Cálculo"));
        QCOMPARE(res.status, CommandStatus::Applied);
    }

    void createEventFailsIfSubjectMissing() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const auto res = proc.process(
            require("Pass create event Examen --start 2026-06-20T08:00:00Z --subject Inexistente"));
        QCOMPARE(res.status, CommandStatus::Failed);
    }

    void createsTaskWithTaskPrefix() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        proc.process(require("Pass create subject Cálculo"));
        const auto res = proc.process(
            require("Pass create task \"Práctica 3\" --due 2026-06-20T22:00:00Z --subject Cálculo"));
        QCOMPARE(res.status, CommandStatus::Applied);

        // El título guardado empieza por "[T] " (convención de tarea).
        const auto id = deterministicId(
            require("Pass create task \"Práctica 3\" --due 2026-06-20T22:00:00Z --subject Cálculo"));
        const auto ev = EventRepository(db.handle()).byId(id);
        QVERIFY(ev.has_value());
        QVERIFY(pass::isTask(*ev));
        QCOMPARE(pass::taskDisplayTitle(*ev), QStringLiteral("Práctica 3"));
    }

    void createTaskRequiresSubject() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        QCOMPARE(proc.process(require("Pass create task X --due 2026-06-20T22:00:00Z")).status,
                 CommandStatus::Failed);
    }

    void createsSessionWithLinkedEvent() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        proc.process(require("Pass create subject Cálculo"));
        const auto res = proc.process(require(
            "Pass create session --start 2026-06-14T16:00:00Z --minutes 50 --subject Cálculo --topic Integrales"));
        QCOMPARE(res.status, CommandStatus::Applied);

        SessionRepository sessions(db.handle());
        const QList<StudySession> all = sessions.all();
        QCOMPARE(all.size(), 1);
        const StudySession s = all.first();
        QCOMPARE(s.plannedMinutes, 50);
        QCOMPARE(s.status, SessionStatus::Planned);
        QVERIFY(!s.linkedEventId.isNull());

        // El evento enlazado existe y referencia a la sesión (source_session_id).
        const auto ev = EventRepository(db.handle()).byId(s.linkedEventId);
        QVERIFY(ev.has_value());
        QCOMPARE(ev->sourceSessionId, s.id);
    }

    void createSessionIsIdempotent() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        const QString text =
            "Pass create session --start 2026-06-14T16:00:00Z --minutes 25";
        proc.process(require(text));
        const auto res = proc.process(require(text));
        QCOMPARE(res.status, CommandStatus::Skipped);
        QCOMPARE(SessionRepository(db.handle()).all().size(), 1);
        // El evento enlazado tampoco se duplicó.
        const auto ev =
            EventRepository(db.handle()).byId(deterministicIdFor(require(text), QStringLiteral("event")));
        QVERIFY(ev.has_value());
    }

    void createSessionRejectsBadMinutes() {
        Database db(QStringLiteral(":memory:"));
        CommandProcessor proc(db.handle(), QString());
        QCOMPARE(proc.process(require("Pass create session --start 2026-06-14T16:00:00Z --minutes cero"))
                     .status,
                 CommandStatus::Failed);
        QCOMPARE(proc.process(require("Pass create session --start 2026-06-14T16:00:00Z --minutes -5"))
                     .status,
                 CommandStatus::Failed);
    }
};

QTEST_GUILESS_MAIN(CommandProcessorTest)
#include "tst_commandprocessor.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/command/CommandProcessor.h"

#include "pass/command/CommandId.h"
#include "pass/domain/CalendarEvent.h"
#include "pass/domain/StudySession.h"
#include "pass/notes/NoteSerializer.h"
#include "pass/notes/VaultService.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>

#include <optional>

namespace pass::command {

namespace {

QString firstPositional(const Command& cmd) {
    return cmd.positional.isEmpty() ? QString() : cmd.positional.first();
}

// Conveniencia para construir un resultado Failed con mensaje.
CommandResult failed(const QString& msg) {
    return {CommandStatus::Failed, msg};
}
CommandResult skipped(const QString& msg) {
    return {CommandStatus::Skipped, msg};
}
CommandResult applied(const QString& msg) {
    return {CommandStatus::Applied, msg};
}

// Parsea una fecha ISO-8601 y la devuelve en UTC. Con sufijo 'Z' (u offset) se
// respeta la zona indicada; sin ella se asume la hora local del dispositivo que
// procesa (limitación documentada en docs/commands.md). Devuelve QDateTime
// invalido si el texto no es una fecha/hora reconocible.
QDateTime parseDateTime(const QString& text) {
    if (text.isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    if (!dt.isValid())
        return {};
    return dt.toUTC();
}

// Resuelve una asignatura por nombre. Devuelve nullopt si está vacía o no existe.
std::optional<Subject> resolveSubject(QSqlDatabase db, const QString& name) {
    if (name.isEmpty())
        return std::nullopt;
    return SubjectRepository(db).byName(name);
}

// Idempotencia de notas: las notas se identifican por nombre de fichero (no por
// UUID), así que embebemos el id del comando en el frontmatter (pass_command_id)
// y, antes de crear, escaneamos la carpeta de notas buscando una que ya lleve
// esa marca. Reprocesar el mismo comando (aquí o en otro dispositivo, una vez
// sincronizado el .md) => Skipped, sin duplicado.
bool noteExistsWithCommandId(const QString& notesDir, const QUuid& id) {
    if (id.isNull() || !QDir(notesDir).exists())
        return false;
    const QString idStr = id.toString(QUuid::WithoutBraces);
    QDirIterator it(notesDir, {QStringLiteral("*.md")}, QDir::Files);
    while (it.hasNext()) {
        it.next();
        QFile f(it.filePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const auto doc = NoteSerializer::parse(QString::fromUtf8(f.readAll()));
        if (NoteSerializer::value(doc, QStringLiteral("pass_command_id")) == idStr)
            return true;
    }
    return false;
}

} // namespace

CommandProcessor::CommandProcessor(QSqlDatabase db, QString repoDir)
    : m_db(std::move(db)), m_repoDir(std::move(repoDir)) {}

CommandResult CommandProcessor::process(const Command& cmd) {
    if (cmd.action != Action::Create)
        return {CommandStatus::Failed, QStringLiteral("acción no soportada")};

    switch (cmd.entity) {
    case Entity::Subject:
        return processSubject(cmd);
    case Entity::Topic:
        return processTopic(cmd);
    case Entity::Event:
        return processEvent(cmd);
    case Entity::Task:
        return processTask(cmd);
    case Entity::Session:
        return processSession(cmd);
    case Entity::Note:
        return processNote(cmd);
    }
    return {CommandStatus::Failed, QStringLiteral("entidad no soportada")};
}

CommandResult CommandProcessor::processSubject(const Command& cmd) {
    const QString name = firstPositional(cmd);
    if (name.isEmpty())
        return {CommandStatus::Failed,
                QStringLiteral("falta el nombre de la asignatura")};

    SubjectRepository repo(m_db);
    const QUuid id = deterministicId(cmd);

    // Idempotencia: mismo comando => mismo id => ya creado => no se duplica.
    if (repo.byId(id))
        return {CommandStatus::Skipped, QStringLiteral("asignatura ya existía (mismo comando)")};
    // Resolución por nombre: si ya hay una con ese nombre, se reutiliza (no es error).
    if (repo.byName(name))
        return {CommandStatus::Skipped,
                QStringLiteral("ya existe una asignatura llamada '%1'").arg(name)};

    Subject s;
    s.id = id;
    s.name = name;
    s.colorHex = cmd.flags.value(QStringLiteral("color"));
    if (!repo.add(s))
        return {CommandStatus::Failed, QStringLiteral("no se pudo crear la asignatura")};
    return {CommandStatus::Applied, QStringLiteral("asignatura '%1' creada").arg(name)};
}

CommandResult CommandProcessor::processTopic(const Command& cmd) {
    const QString subjectName = cmd.flags.value(QStringLiteral("subject"));
    if (subjectName.isEmpty())
        return {CommandStatus::Failed,
                QStringLiteral("falta --subject <asignatura>")};

    SubjectRepository subjects(m_db);
    const auto subject = subjects.byName(subjectName);
    if (!subject)
        return {CommandStatus::Failed,
                QStringLiteral("la asignatura '%1' no existe").arg(subjectName)};

    const QString name = firstPositional(cmd);
    if (name.isEmpty())
        return {CommandStatus::Failed, QStringLiteral("falta el nombre del tema")};

    TopicRepository topics(m_db);
    const QUuid id = deterministicId(cmd);
    if (topics.byId(id))
        return {CommandStatus::Skipped, QStringLiteral("tema ya existía (mismo comando)")};
    if (topics.bySubjectAndName(subject->id, name))
        return {CommandStatus::Skipped,
                QStringLiteral("ya existe un tema '%1' en '%2'").arg(name, subjectName)};

    Topic t;
    t.id = id;
    t.subjectId = subject->id;
    t.name = name;
    if (!topics.add(t))
        return {CommandStatus::Failed, QStringLiteral("no se pudo crear el tema")};
    return {CommandStatus::Applied,
            QStringLiteral("tema '%1' creado en '%2'").arg(name, subjectName)};
}

// --- create event / task ------------------------------------------------------
// Una tarea es un evento cuyo título empieza por "[T] " (convención de Pass), con
// asignatura obligatoria y start_utc = fecha de entrega. Ambos comparten lógica.

CommandResult CommandProcessor::processEvent(const Command& cmd) {
    const QString title = firstPositional(cmd);
    if (title.isEmpty())
        return failed(QStringLiteral("falta el título del evento"));

    const QString startStr = cmd.flags.value(QStringLiteral("start"));
    if (startStr.isEmpty())
        return failed(QStringLiteral("falta --start <fecha ISO>"));
    const QDateTime start = parseDateTime(startStr);
    if (!start.isValid())
        return failed(QStringLiteral("fecha de inicio inválida: '%1'").arg(startStr));

    const QString endStr = cmd.flags.value(QStringLiteral("end"));
    QDateTime end;
    if (endStr.isEmpty())
        end = cmd.boolFlags.contains(QStringLiteral("all-day")) ? start : start.addSecs(3600);
    else {
        end = parseDateTime(endStr);
        if (!end.isValid())
            return failed(QStringLiteral("fecha de fin inválida: '%1'").arg(endStr));
    }

    CalendarEvent e;
    e.id = deterministicId(cmd);
    EventRepository events(m_db);
    if (events.byId(e.id))
        return skipped(QStringLiteral("evento ya existía (mismo comando)"));

    e.title = title;
    e.description = cmd.flags.value(QStringLiteral("desc"));
    e.startUtc = start;
    e.endUtc = end;
    e.allDay = cmd.boolFlags.contains(QStringLiteral("all-day"));
    if (const auto s = resolveSubject(m_db, cmd.flags.value(QStringLiteral("subject"))))
        e.subjectId = s->id;
    else if (cmd.flags.contains(QStringLiteral("subject")))
        return failed(QStringLiteral("la asignatura '%1' no existe")
                          .arg(cmd.flags.value(QStringLiteral("subject"))));

    if (!events.add(e))
        return failed(QStringLiteral("no se pudo crear el evento"));
    return applied(QStringLiteral("evento '%1' creado").arg(title));
}

CommandResult CommandProcessor::processTask(const Command& cmd) {
    const QString rawTitle = firstPositional(cmd);
    if (rawTitle.isEmpty())
        return failed(QStringLiteral("falta el título de la tarea"));

    const QString subjectName = cmd.flags.value(QStringLiteral("subject"));
    if (subjectName.isEmpty())
        return failed(QStringLiteral("una tarea requiere --subject <asignatura>"));
    const auto subject = resolveSubject(m_db, subjectName);
    if (!subject)
        return failed(QStringLiteral("la asignatura '%1' no existe").arg(subjectName));

    const QString dueStr = cmd.flags.value(QStringLiteral("due"));
    if (dueStr.isEmpty())
        return failed(QStringLiteral("falta --due <fecha ISO de entrega>"));
    const QDateTime due = parseDateTime(dueStr);
    if (!due.isValid())
        return failed(QStringLiteral("fecha de entrega inválida: '%1'").arg(dueStr));

    CalendarEvent e;
    e.id = deterministicId(cmd);
    EventRepository events(m_db);
    if (events.byId(e.id))
        return skipped(QStringLiteral("tarea ya existía (mismo comando)"));

    e.title = kTaskTitlePrefix + QLatin1Char(' ') + rawTitle;
    e.description = cmd.flags.value(QStringLiteral("desc"));
    e.startUtc = due;
    e.endUtc = due;
    e.subjectId = subject->id;

    if (!events.add(e))
        return failed(QStringLiteral("no se pudo crear la tarea"));
    return applied(QStringLiteral("tarea '%1' creada").arg(rawTitle));
}

// --- create session -----------------------------------------------------------
// Crea dos recursos enlazados en una transacción: un evento local (con
// source_session_id) y una sesión planificada (con event_id). Ambos ids derivan
// del comando (sesión sin sal; evento con sal "event") para que reproducir el
// comando reproduzca los mismos ids => idempotente.

CommandResult CommandProcessor::processSession(const Command& cmd) {
    const QString startStr = cmd.flags.value(QStringLiteral("start"));
    if (startStr.isEmpty())
        return failed(QStringLiteral("falta --start <fecha ISO>"));
    const QDateTime start = parseDateTime(startStr);
    if (!start.isValid())
        return failed(QStringLiteral("fecha de inicio inválida: '%1'").arg(startStr));

    int minutes = 50;
    if (cmd.flags.contains(QStringLiteral("minutes"))) {
        bool ok = false;
        const int m = cmd.flags.value(QStringLiteral("minutes")).toInt(&ok);
        if (!ok || m <= 0)
            return failed(QStringLiteral("--minutes debe ser un entero positivo"));
        minutes = m;
    }

    const auto subject =
        resolveSubject(m_db, cmd.flags.value(QStringLiteral("subject")));
    if (cmd.flags.contains(QStringLiteral("subject")) && !subject)
        return failed(QStringLiteral("la asignatura '%1' no existe")
                          .arg(cmd.flags.value(QStringLiteral("subject"))));

    QUuid strategyId;
    if (cmd.flags.contains(QStringLiteral("strategy"))) {
        strategyId = QUuid(cmd.flags.value(QStringLiteral("strategy")));
        if (strategyId.isNull())
            return failed(QStringLiteral("--strategy debe ser un UUID válido"));
    }

    const QUuid sessionId = deterministicId(cmd);
    const QUuid eventId = deterministicIdFor(cmd, QStringLiteral("event"));
    SessionRepository sessions(m_db);
    if (sessions.byId(sessionId))
        return skipped(QStringLiteral("sesión ya existía (mismo comando)"));

    CalendarEvent ev;
    ev.id = eventId;
    ev.title = subject ? QStringLiteral("Sesión: %1").arg(cmd.flags.value(QStringLiteral("subject")))
                       : QStringLiteral("Sesión");
    ev.startUtc = start;
    ev.endUtc = start.addSecs(minutes * 60);
    ev.subjectId = subject ? subject->id : QUuid();
    ev.sourceSessionId = sessionId;

    StudySession s;
    s.id = sessionId;
    s.subjectId = subject ? subject->id : QUuid();
    s.strategyId = strategyId;
    s.topic = cmd.flags.value(QStringLiteral("topic"));
    s.plannedMinutes = minutes;
    s.actualSeconds = 0;
    s.startedAt = start;
    s.status = SessionStatus::Planned;
    s.linkedEventId = eventId;

    // Transacción: ambos o ninguno. El evento puede existir ya de un intento
    // previo parcial; en ese caso se reutiliza y solo se inserta la sesión.
    if (!m_db.transaction())
        return failed(QStringLiteral("no se pudo iniciar la transacción"));
    EventRepository events(m_db);
    bool ok = true;
    if (!events.byId(eventId))
        ok = events.add(ev);
    if (ok && !sessions.byId(sessionId))
        ok = sessions.add(s);
    if (!ok || !m_db.commit()) {
        m_db.rollback();
        return failed(QStringLiteral("no se pudo crear la sesión"));
    }
    return applied(QStringLiteral("sesión planificada creada (%1 min)").arg(minutes));
}

// --- create note --------------------------------------------------------------
// Escribe un `.md` directamente en `<repoDir>/notes/` (NO en el vault): el espejo
// de notas de GitSyncService lo traerá al vault del usuario. Replica la convención
// de VaultService::createNote (nombre con fecha, frontmatter, secciones) y añade
// `pass_command_id` al frontmatter como marca de idempotencia.

CommandResult CommandProcessor::processNote(const Command& cmd) {
    const QString topic = firstPositional(cmd);
    const QString subjectName = cmd.flags.value(QStringLiteral("subject"));
    const QString body = cmd.flags.value(QStringLiteral("body"));

    const QUuid commandId = deterministicId(cmd);
    const QString repoNotes = m_repoDir + QStringLiteral("/notes");

    if (noteExistsWithCommandId(repoNotes, commandId))
        return skipped(QStringLiteral("nota ya existía (mismo comando)"));

    // Asignatura opcional: si se indica, debe existir.
    if (!subjectName.isEmpty() && !resolveSubject(m_db, subjectName))
        return failed(QStringLiteral("la asignatura '%1' no existe").arg(subjectName));

    const QDateTime now = QDateTime::currentDateTime();
    const QString safeTopic = VaultService::sanitizeTitle(topic);
    const QString safeSubject = VaultService::sanitizeTitle(subjectName);

    // Nombre de fichero: "YYYY-MM-DD HHmm - <Asignatura> - <Tema>.md".
    QStringList stemParts{now.toString(QStringLiteral("yyyy-MM-dd HHmm"))};
    if (!safeSubject.isEmpty())
        stemParts << safeSubject;
    stemParts << (safeTopic.isEmpty() ? (safeSubject.isEmpty() ? QStringLiteral("Nota")
                                                               : safeSubject)
                                      : safeTopic);
    const QString stem = stemParts.join(QStringLiteral(" - "));

    if (!QDir().mkpath(repoNotes))
        return failed(QStringLiteral("no se pudo crear la carpeta de notas"));
    QString fileName = stem + QStringLiteral(".md");
    for (int n = 2; QFile::exists(repoNotes + QLatin1Char('/') + fileName); ++n)
        fileName = QStringLiteral("%1 (%2).md").arg(stem).arg(n);

    NoteSerializer::Document doc;
    NoteSerializer::setValue(doc, QStringLiteral("created"), now.toString(Qt::ISODate));
    NoteSerializer::setValue(doc, QStringLiteral("app"), QStringLiteral("pass"));
    NoteSerializer::setValue(doc, QStringLiteral("pass_command_id"),
                             commandId.toString(QUuid::WithoutBraces));

    const QString heading = safeTopic.isEmpty()
                                ? (safeSubject.isEmpty() ? QStringLiteral("Nota") : topic.trimmed())
                                : topic.trimmed();
    const QString dateText = now.toString(QStringLiteral("dd/MM/yyyy HH:mm"));

    if (!safeSubject.isEmpty()) {
        NoteSerializer::setValue(doc, QStringLiteral("subject"), subjectName.trimmed());
        QString tag = safeSubject.toLower();
        tag.replace(QLatin1Char(' '), QLatin1Char('-'));
        NoteSerializer::setValue(doc, QStringLiteral("tags"),
                                 QStringLiteral("[pass, %1]").arg(tag));
        doc.body = QStringLiteral("\n# %1\n\n> Asignatura: %2 · Creada: %3\n\n## Apuntes\n\n%4\n\n"
                                  "## Dudas\n\n")
                       .arg(heading, subjectName.trimmed(), dateText, body);
    } else {
        NoteSerializer::setValue(doc, QStringLiteral("tags"), QStringLiteral("[pass]"));
        doc.body = QStringLiteral("\n# %1\n\n> Creada: %2\n\n%3\n\n").arg(heading, dateText, body);
    }

    QSaveFile f(repoNotes + QLatin1Char('/') + fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return failed(QStringLiteral("no se pudo abrir la nota para escribir"));
    QTextStream out(&f);
    out << NoteSerializer::serialize(doc);
    out.flush();
    if (!f.commit())
        return failed(QStringLiteral("no se pudo guardar la nota"));
    return applied(QStringLiteral("nota '%1' creada").arg(fileName));
}

} // namespace pass::command

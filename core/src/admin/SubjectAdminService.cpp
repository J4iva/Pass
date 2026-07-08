// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/admin/SubjectAdminService.h"

#include "pass/calendar/CalendarService.h"
#include "pass/notes/NoteSerializer.h"
#include "pass/notes/VaultService.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QDateTime>
#include <QSqlQuery>

namespace pass {

namespace {

QString nowIsoUtc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// Slug del tag de asignatura, igual que en VaultService::createNote.
QString subjectSlug(const QString& name) {
    QString slug = VaultService::sanitizeTitle(name).toLower();
    slug.replace(QLatin1Char(' '), QLatin1Char('-'));
    return slug;
}

} // namespace

SubjectAdminService::SubjectAdminService(QSqlDatabase db, QString vaultPath, QString vaultSubfolder,
                                         CalendarService* calendar)
    : m_db(std::move(db)), m_vaultPath(std::move(vaultPath)),
      m_vaultSubfolder(std::move(vaultSubfolder)), m_calendar(calendar) {}

SubjectAdminService::Impact SubjectAdminService::impactOf(const QUuid& subjectId) const {
    Impact impact;
    const QString id = subjectId.toString(QUuid::WithoutBraces);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*), SUM(title LIKE '[T]%') FROM events WHERE subject_id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        impact.events = q.value(0).toInt();
        impact.tasks = q.value(1).toInt();
    }
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM sessions WHERE subject_id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        impact.sessions = q.value(0).toInt();
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM topics WHERE subject_id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        impact.topics = q.value(0).toInt();

    SubjectRepository subjects(m_db);
    if (const auto s = subjects.byId(subjectId))
        impact.notes = notesForSubject(s->name).size();
    return impact;
}

bool SubjectAdminService::rename(const QUuid& id, const QString& newName, QString& error) {
    const QString name = newName.trimmed();
    if (name.isEmpty()) {
        error = QStringLiteral("El nombre no puede estar vacío.");
        return false;
    }
    SubjectRepository subjects(m_db);
    const auto current = subjects.byId(id);
    if (!current) {
        error = QStringLiteral("La asignatura ya no existe.");
        return false;
    }
    if (current->name == name)
        return true; // sin cambios
    // Pre-chequeo de unicidad para un mensaje claro (la UNIQUE de la BD también lo
    // impediría, pero con un error genérico).
    for (const auto& s : subjects.all(true)) {
        if (s.id != id && s.name.compare(name, Qt::CaseInsensitive) == 0) {
            error = QStringLiteral("Ya existe una asignatura llamada «%1».").arg(name);
            return false;
        }
    }

    const QString oldName = current->name;
    Subject updated = *current;
    updated.name = name;
    if (!subjects.update(updated)) {
        error = QStringLiteral("No se pudo renombrar la asignatura.");
        return false;
    }
    rewriteNotesSubject(oldName, name);
    return true;
}

bool SubjectAdminService::remove(const QUuid& id, QString& error) {
    SubjectRepository subjects(m_db);
    const auto current = subjects.byId(id);
    if (!current) {
        error = QStringLiteral("La asignatura ya no existe.");
        return false;
    }
    const QString sid = id.toString(QUuid::WithoutBraces);

    // 1) Tareas ([T]): borrado real, respetando Google si hay router de calendario.
    QList<QUuid> taskIds;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT id FROM events WHERE subject_id = ? AND title LIKE '[T]%'"));
        q.addBindValue(sid);
        if (q.exec())
            while (q.next())
                taskIds.append(QUuid::fromString(q.value(0).toString()));
    }
    EventRepository events(m_db);
    for (const QUuid& taskId : taskIds) {
        if (m_calendar)
            m_calendar->removeEvent(taskId);
        else
            events.remove(taskId);
    }

    // 2) Temas de la asignatura (con tombstone cada uno).
    TopicRepository topics(m_db);
    for (const auto& t : topics.bySubject(id))
        topics.remove(t.id);

    // 3) Notas .md de la asignatura.
    if (!m_vaultPath.isEmpty()) {
        VaultService vault(m_vaultPath, m_vaultSubfolder);
        for (const QString& file : notesForSubject(current->name))
            vault.deleteNote(file);
    }

    // 4) Conservar histórico: eventos no-tarea y sesiones quedan sin asignatura.
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("UPDATE events SET subject_id = NULL, updated_at = ? "
                                 "WHERE subject_id = ?"));
        q.addBindValue(nowIsoUtc());
        q.addBindValue(sid);
        q.exec();
        q.prepare(QStringLiteral("UPDATE sessions SET subject_id = NULL, updated_at = ? "
                                 "WHERE subject_id = ?"));
        q.addBindValue(nowIsoUtc());
        q.addBindValue(sid);
        q.exec();
    }

    // 5) La asignatura (con tombstone).
    if (!subjects.remove(id)) {
        error = QStringLiteral("No se pudo eliminar la asignatura.");
        return false;
    }
    return true;
}

QStringList SubjectAdminService::notesForSubject(const QString& name) const {
    QStringList result;
    if (m_vaultPath.isEmpty())
        return result;
    const VaultService vault(m_vaultPath, m_vaultSubfolder);
    for (const auto& note : vault.notes()) {
        const auto content = vault.readNote(note.fileName);
        if (!content)
            continue;
        const NoteSerializer::Document doc = NoteSerializer::parse(*content);
        if (NoteSerializer::value(doc, QStringLiteral("subject")) == name)
            result.append(note.fileName);
    }
    return result;
}

int SubjectAdminService::rewriteNotesSubject(const QString& oldName, const QString& newName) {
    if (m_vaultPath.isEmpty())
        return 0;
    VaultService vault(m_vaultPath, m_vaultSubfolder);
    const QString oldSlug = subjectSlug(oldName);
    const QString newSlug = subjectSlug(newName);
    const QString safeOld = VaultService::sanitizeTitle(oldName);
    const QString safeNew = VaultService::sanitizeTitle(newName);

    int touched = 0;
    for (const QString& file : notesForSubject(oldName)) {
        const auto content = vault.readNote(file);
        if (!content)
            continue;
        NoteSerializer::Document doc = NoteSerializer::parse(*content);

        NoteSerializer::setValue(doc, QStringLiteral("subject"), newName);
        // Tag de asignatura: sustituye el slug viejo conservando el resto de tags.
        QString tags = NoteSerializer::value(doc, QStringLiteral("tags"));
        if (!tags.isEmpty() && tags.contains(oldSlug)) {
            tags.replace(oldSlug, newSlug);
            NoteSerializer::setValue(doc, QStringLiteral("tags"), tags);
        }
        // Línea "> Asignatura: <old>" del cuerpo de la plantilla de estudio.
        doc.body.replace(QStringLiteral("Asignatura: ") + oldName,
                         QStringLiteral("Asignatura: ") + newName);

        // Nombre de fichero: el segmento de asignatura es la parte tras la fecha.
        QString newFile = file;
        QString stem = file;
        if (stem.endsWith(QStringLiteral(".md")))
            stem.chop(3);
        QStringList parts = stem.split(QStringLiteral(" - "));
        if (parts.size() >= 2 && parts[1] == safeOld && !safeNew.isEmpty()) {
            parts[1] = safeNew;
            const QString candidate = parts.join(QStringLiteral(" - ")) + QStringLiteral(".md");
            if (candidate != file && !vault.readNote(candidate)) // no pisar otra nota
                newFile = candidate;
        }

        const QString serialized = NoteSerializer::serialize(doc);
        if (newFile != file) {
            if (vault.writeNote(newFile, serialized)) {
                vault.deleteNote(file);
                ++touched;
            }
        } else if (vault.writeNote(file, serialized)) {
            ++touched;
        }
    }
    return touched;
}

} // namespace pass

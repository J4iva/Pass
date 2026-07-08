// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/SessionRepository.h"

#include "pass/sync/DataChangeNotifier.h"
#include "pass/sync/Tombstones.h"

#include <QSqlQuery>
#include <QTimeZone>

namespace pass {

namespace {

constexpr auto kColumns = "id, subject_id, strategy_id, topic, planned_min, actual_sec, "
                          "started_at, ended_at, status, event_id, updated_at, "
                          "resume_phase_index, resume_phase_elapsed_sec";

QString uuidOrNull(const QUuid& id) {
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QString isoOrNull(const QDateTime& dt) {
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODate) : QString();
}

StudySession fromRow(const QSqlQuery& q) {
    StudySession s;
    s.id = QUuid::fromString(q.value(0).toString());
    s.subjectId = QUuid::fromString(q.value(1).toString());
    s.strategyId = QUuid::fromString(q.value(2).toString());
    s.topic = q.value(3).toString();
    s.plannedMinutes = q.value(4).toInt();
    s.actualSeconds = q.value(5).toInt();
    s.startedAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    s.startedAt.setTimeZone(QTimeZone::utc());
    s.endedAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
    s.endedAt.setTimeZone(QTimeZone::utc());
    s.status = sessionStatusFromString(q.value(8).toString());
    s.linkedEventId = QUuid::fromString(q.value(9).toString());
    s.updatedAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODate);
    s.updatedAt.setTimeZone(QTimeZone::utc());
    // Columnas nullable: NULL (no retomable) → -1 / 0.
    s.resumePhaseIndex = q.value(11).isNull() ? -1 : q.value(11).toInt();
    s.resumeElapsedSec = q.value(12).isNull() ? 0 : q.value(12).toInt();
    return s;
}

// Vincula las columnas-valor (todas menos id), incluido updated_at = ahora UTC:
// toda mutación local pisa la marca para que el last-writer-wins funcione.
void bindSession(QSqlQuery& q, const StudySession& s) {
    q.addBindValue(uuidOrNull(s.subjectId));
    q.addBindValue(uuidOrNull(s.strategyId));
    q.addBindValue(s.topic);
    q.addBindValue(s.plannedMinutes);
    q.addBindValue(s.actualSeconds);
    q.addBindValue(isoOrNull(s.startedAt));
    q.addBindValue(isoOrNull(s.endedAt));
    q.addBindValue(sessionStatusToString(s.status));
    q.addBindValue(uuidOrNull(s.linkedEventId));
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    // Sin progreso de reanudación → NULL en ambas columnas.
    if (s.resumePhaseIndex < 0) {
        q.addBindValue(QVariant());
        q.addBindValue(QVariant());
    } else {
        q.addBindValue(s.resumePhaseIndex);
        q.addBindValue(s.resumeElapsedSec);
    }
}

} // namespace

SessionRepository::SessionRepository(QSqlDatabase db) : m_db(std::move(db)) {}

QList<StudySession> SessionRepository::all() const {
    QList<StudySession> result;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM sessions ORDER BY started_at DESC")
                    .arg(QLatin1String(kColumns))))
        return result;
    while (q.next())
        result.append(fromRow(q));
    return result;
}

std::optional<StudySession> SessionRepository::byId(const QUuid& id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM sessions WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool SessionRepository::add(const StudySession& s) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions(id, subject_id, strategy_id, topic, planned_min, actual_sec, "
        "started_at, ended_at, status, event_id, updated_at, "
        "resume_phase_index, resume_phase_elapsed_sec) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(s.id.toString(QUuid::WithoutBraces));
    bindSession(q, s);
    if (!q.exec())
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool SessionRepository::update(const StudySession& s) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE sessions SET subject_id = ?, strategy_id = ?, topic = ?, planned_min = ?, "
        "actual_sec = ?, started_at = ?, ended_at = ?, status = ?, event_id = ?, updated_at = ?, "
        "resume_phase_index = ?, resume_phase_elapsed_sec = ? "
        "WHERE id = ?"));
    bindSession(q, s);
    q.addBindValue(s.id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1)
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

qint64 SessionRepository::totalSecondsForEvent(const QUuid& eventId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(actual_sec), 0) FROM sessions WHERE event_id = ?"));
    q.addBindValue(eventId.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toLongLong();
}

bool SessionRepository::remove(const QUuid& id) {
    // Borrado + tombstone atómicos, para propagar el borrado a otros dispositivos.
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1 || !insertTombstone(m_db, entity::kSessions, id)) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit())
        return false;
    DataChangeNotifier::instance().notifyChanged();
    return true;
}

} // namespace pass

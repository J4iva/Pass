// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/SessionRepository.h"

#include <QSqlQuery>
#include <QTimeZone>

namespace pass {

namespace {

constexpr auto kColumns = "id, subject_id, strategy_id, topic, planned_min, actual_sec, "
                          "started_at, ended_at, status, event_id";

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
    return s;
}

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
    q.prepare(QStringLiteral("INSERT INTO sessions(id, subject_id, strategy_id, topic, "
                             "planned_min, actual_sec, started_at, ended_at, status, event_id) "
                             "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(s.id.toString(QUuid::WithoutBraces));
    bindSession(q, s);
    return q.exec();
}

bool SessionRepository::update(const StudySession& s) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE sessions SET subject_id = ?, strategy_id = ?, topic = ?, planned_min = ?, "
        "actual_sec = ?, started_at = ?, ended_at = ?, status = ?, event_id = ? WHERE id = ?"));
    bindSession(q, s);
    q.addBindValue(s.id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

bool SessionRepository::remove(const QUuid& id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

} // namespace pass

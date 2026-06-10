// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/EventRepository.h"

#include <QSqlQuery>
#include <QTimeZone>

namespace pass {

namespace {

constexpr auto kColumns = "id, provider, external_id, title, description, start_utc, end_utc, "
                          "all_day, rrule, subject_id, source_session_id, updated_at";

QString uuidOrNull(const QUuid& id) {
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

QString toIso(const QDateTime& dt) {
    return dt.toUTC().toString(Qt::ISODate);
}

QDateTime fromIso(const QString& text) {
    QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

CalendarEvent fromRow(const QSqlQuery& q) {
    CalendarEvent e;
    e.id = QUuid::fromString(q.value(0).toString());
    e.provider = q.value(1).toString();
    e.externalId = q.value(2).toString();
    e.title = q.value(3).toString();
    e.description = q.value(4).toString();
    e.startUtc = fromIso(q.value(5).toString());
    e.endUtc = fromIso(q.value(6).toString());
    e.allDay = q.value(7).toBool();
    e.rrule = q.value(8).toString();
    e.subjectId = QUuid::fromString(q.value(9).toString());
    e.sourceSessionId = QUuid::fromString(q.value(10).toString());
    e.updatedAt = fromIso(q.value(11).toString());
    return e;
}

void bindEvent(QSqlQuery& q, const CalendarEvent& e) {
    q.addBindValue(e.provider);
    q.addBindValue(e.externalId);
    q.addBindValue(e.title);
    q.addBindValue(e.description);
    q.addBindValue(toIso(e.startUtc));
    q.addBindValue(toIso(e.endUtc));
    q.addBindValue(e.allDay ? 1 : 0);
    q.addBindValue(e.rrule);
    q.addBindValue(uuidOrNull(e.subjectId));
    q.addBindValue(uuidOrNull(e.sourceSessionId));
    q.addBindValue(toIso(QDateTime::currentDateTimeUtc()));
}

} // namespace

EventRepository::EventRepository(QSqlDatabase db) : m_db(std::move(db)) {}

QList<CalendarEvent> EventRepository::between(const QDateTime& fromUtc,
                                              const QDateTime& toUtc) const {
    QList<CalendarEvent> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM events WHERE start_utc < ? AND end_utc > ? "
                             "ORDER BY start_utc")
                  .arg(QLatin1String(kColumns)));
    q.addBindValue(toIso(toUtc));
    q.addBindValue(toIso(fromUtc));
    if (!q.exec())
        return result;
    while (q.next())
        result.append(fromRow(q));
    return result;
}

std::optional<CalendarEvent> EventRepository::byId(const QUuid& id) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM events WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool EventRepository::add(const CalendarEvent& e) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO events(id, provider, external_id, title, description, "
                             "start_utc, end_utc, all_day, rrule, subject_id, source_session_id, "
                             "updated_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(e.id.toString(QUuid::WithoutBraces));
    bindEvent(q, e);
    return q.exec();
}

bool EventRepository::update(const CalendarEvent& e) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE events SET provider = ?, external_id = ?, title = ?, description = ?, "
        "start_utc = ?, end_utc = ?, all_day = ?, rrule = ?, subject_id = ?, "
        "source_session_id = ?, updated_at = ? WHERE id = ?"));
    bindEvent(q, e);
    q.addBindValue(e.id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

bool EventRepository::remove(const QUuid& id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM events WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

} // namespace pass

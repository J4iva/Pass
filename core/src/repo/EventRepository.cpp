// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/EventRepository.h"

#include "pass/sync/DataChangeNotifier.h"
#include "pass/sync/Tombstones.h"

#include <QSqlQuery>
#include <QTimeZone>

namespace pass {

namespace {

constexpr auto kColumns = "id, provider, external_id, title, description, start_utc, end_utc, "
                          "all_day, rrule, subject_id, source_session_id, updated_at, etag";

QString uuidOrNull(const QUuid& id) {
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

// Solo los eventos puramente locales se exportan al repo de sync: los espejo de
// Google los re-sincroniza cada dispositivo desde Google. Por eso solo este tipo
// de evento notifica cambios y deja tombstone al borrarse.
bool isLocalOnly(const CalendarEvent& e) {
    return e.provider == QStringLiteral("local") && e.externalId.isEmpty();
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
    e.etag = q.value(12).toString();
    return e;
}

// Vincula las columnas-valor (todas menos id), en el orden de kColumns sin id.
// `updatedAtUtc` se pasa explícito: las mutaciones locales usan "ahora"; el
// upsert de sincronización conserva la marca que viene de Google.
void bindEvent(QSqlQuery& q, const CalendarEvent& e, const QString& updatedAtUtc) {
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
    q.addBindValue(updatedAtUtc);
    q.addBindValue(e.etag);
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
                             "updated_at, etag) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(e.id.toString(QUuid::WithoutBraces));
    bindEvent(q, e, toIso(QDateTime::currentDateTimeUtc()));
    if (!q.exec())
        return false;
    if (isLocalOnly(e))
        DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool EventRepository::update(const CalendarEvent& e) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE events SET provider = ?, external_id = ?, title = ?, description = ?, "
        "start_utc = ?, end_utc = ?, all_day = ?, rrule = ?, subject_id = ?, "
        "source_session_id = ?, updated_at = ?, etag = ? WHERE id = ?"));
    bindEvent(q, e, toIso(QDateTime::currentDateTimeUtc()));
    q.addBindValue(e.id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1)
        return false;
    if (isLocalOnly(e))
        DataChangeNotifier::instance().notifyChanged();
    return true;
}

bool EventRepository::remove(const QUuid& id) {
    // Solo los eventos locales dejan tombstone y notifican: los espejo de Google
    // se borran sin más (cada dispositivo los re-sincroniza desde Google).
    const auto existing = byId(id);
    const bool localOnly = existing && isLocalOnly(*existing);

    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM events WHERE id = ?"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || q.numRowsAffected() != 1) {
        m_db.rollback();
        return false;
    }
    if (localOnly && !insertTombstone(m_db, entity::kEvents, id)) {
        m_db.rollback();
        return false;
    }
    if (!m_db.commit())
        return false;
    if (localOnly)
        DataChangeNotifier::instance().notifyChanged();
    return true;
}

std::optional<CalendarEvent> EventRepository::byExternalId(const QString& provider,
                                                           const QString& externalId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM events WHERE provider = ? AND external_id = ?")
                  .arg(QLatin1String(kColumns)));
    q.addBindValue(provider);
    q.addBindValue(externalId);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool EventRepository::upsertByExternalId(const CalendarEvent& e) {
    // Conserva la marca temporal y el etag que vienen de Google (no "ahora").
    const QString updatedAt = toIso(e.updatedAt.isValid() ? e.updatedAt
                                                          : QDateTime::currentDateTimeUtc());
    if (const auto existing = byExternalId(e.provider, e.externalId)) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "UPDATE events SET title = ?, description = ?, start_utc = ?, end_utc = ?, "
            "all_day = ?, rrule = ?, subject_id = ?, updated_at = ?, etag = ? "
            "WHERE provider = ? AND external_id = ?"));
        q.addBindValue(e.title);
        q.addBindValue(e.description);
        q.addBindValue(toIso(e.startUtc));
        q.addBindValue(toIso(e.endUtc));
        q.addBindValue(e.allDay ? 1 : 0);
        q.addBindValue(e.rrule);
        q.addBindValue(uuidOrNull(e.subjectId));
        q.addBindValue(updatedAt);
        q.addBindValue(e.etag);
        q.addBindValue(e.provider);
        q.addBindValue(e.externalId);
        return q.exec();
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO events(id, provider, external_id, title, description, "
                             "start_utc, end_utc, all_day, rrule, subject_id, source_session_id, "
                             "updated_at, etag) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue((e.id.isNull() ? QUuid::createUuid() : e.id).toString(QUuid::WithoutBraces));
    bindEvent(q, e, updatedAt);
    return q.exec();
}

bool EventRepository::removeByExternalId(const QString& provider, const QString& externalId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM events WHERE provider = ? AND external_id = ?"));
    q.addBindValue(provider);
    q.addBindValue(externalId);
    return q.exec() && q.numRowsAffected() >= 1;
}

} // namespace pass

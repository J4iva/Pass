// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/stats/StatsService.h"

#include <QDateTime>
#include <QHash>
#include <QSqlQuery>
#include <QTimeZone>

namespace pass {

StatsService::StatsService(QSqlDatabase db) : m_db(std::move(db)) {}

QList<SubjectHours> StatsService::hoursBySubject() const {
    QList<SubjectHours> result;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT s.subject_id, COALESCE(sub.name, ''), COALESCE(sub.color, ''), "
            "SUM(s.actual_sec) "
            "FROM sessions s LEFT JOIN subjects sub ON sub.id = s.subject_id "
            "WHERE s.status != 'planned' AND s.actual_sec > 0 "
            "GROUP BY s.subject_id ORDER BY SUM(s.actual_sec) DESC")))
        return result;
    while (q.next()) {
        SubjectHours row;
        row.subjectId = QUuid::fromString(q.value(0).toString());
        row.subjectName = q.value(1).toString();
        row.colorHex = q.value(2).toString();
        row.workSeconds = q.value(3).toLongLong();
        result.append(row);
    }
    return result;
}

QList<DailyMinutes> StatsService::minutesPerDay(int days) const {
    // La fecha "del día" se decide en hora local del usuario, así que la
    // agregación se hace aquí y no en SQL (la DB guarda UTC).
    QHash<QDate, int> byDay;
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-days);

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT started_at, actual_sec FROM sessions "
                             "WHERE status != 'planned' AND actual_sec > 0 AND started_at >= ?"));
    q.addBindValue(cutoff.toString(Qt::ISODate));
    if (!q.exec())
        return {};
    while (q.next()) {
        QDateTime started = QDateTime::fromString(q.value(0).toString(), Qt::ISODate);
        started.setTimeZone(QTimeZone::utc());
        byDay[started.toLocalTime().date()] += q.value(1).toInt() / 60;
    }

    QList<DailyMinutes> result;
    const QDate today = QDate::currentDate();
    for (int i = days - 1; i >= 0; --i) {
        const QDate day = today.addDays(-i);
        result.append({day, byDay.value(day, 0)});
    }
    return result;
}

int StatsService::completedSessionCount() const {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM sessions WHERE status = 'completed'")) ||
        !q.next())
        return 0;
    return q.value(0).toInt();
}

qint64 StatsService::totalWorkSeconds() const {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT COALESCE(SUM(actual_sec), 0) FROM sessions WHERE status != 'planned'")) ||
        !q.next())
        return 0;
    return q.value(0).toLongLong();
}

} // namespace pass

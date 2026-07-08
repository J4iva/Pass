// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/GoogleEventMapper.h"

#include <QJsonValue>
#include <QTimeZone>

namespace pass::GoogleEventMapper {

namespace {

// "2026-06-10" → medianoche local de ese día en UTC (mismo criterio que la UI
// de Pass para los eventos de todo el día).
QDateTime allDayToUtc(const QString& date) {
    return QDateTime(QDate::fromString(date, Qt::ISODate), QTime(0, 0)).toUTC();
}

// "2026-06-10T09:00:00+02:00" / "...Z" → instante en UTC.
QDateTime dateTimeToUtc(const QString& rfc3339) {
    return QDateTime::fromString(rfc3339, Qt::ISODate).toUTC();
}

} // namespace

CalendarEvent fromJson(const QJsonObject& obj, bool* cancelled) {
    CalendarEvent e;
    e.provider = QStringLiteral("google");
    e.externalId = obj.value(QStringLiteral("id")).toString();
    e.etag = obj.value(QStringLiteral("etag")).toString();
    e.title = obj.value(QStringLiteral("summary")).toString();
    e.description = obj.value(QStringLiteral("description")).toString();

    if (cancelled)
        *cancelled = obj.value(QStringLiteral("status")).toString() == QStringLiteral("cancelled");

    const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
    const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
    const QString startDate = start.value(QStringLiteral("date")).toString();
    if (!startDate.isEmpty()) {
        e.allDay = true;
        e.startUtc = allDayToUtc(startDate);
        e.endUtc = allDayToUtc(end.value(QStringLiteral("date")).toString());
    } else {
        e.allDay = false;
        e.startUtc = dateTimeToUtc(start.value(QStringLiteral("dateTime")).toString());
        e.endUtc = dateTimeToUtc(end.value(QStringLiteral("dateTime")).toString());
    }

    const QString updated = obj.value(QStringLiteral("updated")).toString();
    if (!updated.isEmpty())
        e.updatedAt = dateTimeToUtc(updated);

    return e;
}

QJsonObject toJson(const CalendarEvent& event) {
    QJsonObject obj;
    obj.insert(QStringLiteral("summary"), event.title);
    if (!event.description.isEmpty())
        obj.insert(QStringLiteral("description"), event.description);

    QJsonObject start;
    QJsonObject end;
    if (event.allDay) {
        // Google espera la fecha local del día (fin exclusivo, como Pass).
        start.insert(QStringLiteral("date"),
                     event.startUtc.toLocalTime().date().toString(Qt::ISODate));
        end.insert(QStringLiteral("date"), event.endUtc.toLocalTime().date().toString(Qt::ISODate));
    } else {
        start.insert(QStringLiteral("dateTime"), event.startUtc.toUTC().toString(Qt::ISODate));
        end.insert(QStringLiteral("dateTime"), event.endUtc.toUTC().toString(Qt::ISODate));
    }
    obj.insert(QStringLiteral("start"), start);
    obj.insert(QStringLiteral("end"), end);
    return obj;
}

} // namespace pass::GoogleEventMapper

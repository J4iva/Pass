// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace pass {

struct CalendarEvent {
    QUuid id;
    QString provider = QStringLiteral("local");
    QString externalId; // id en el proveedor remoto (Google...), fase 2
    QString etag;       // ETag del recurso remoto, para If-Match en write-through
    QString title;
    QString description;
    QDateTime startUtc; // siempre UTC
    QDateTime endUtc;   // siempre UTC
    bool allDay = false;
    QString rrule; // RRULE simplificada; vacía = sin recurrencia (MVP)
    QUuid subjectId;
    QUuid sourceSessionId; // sesión de trabajo planificada que originó el evento
    QDateTime updatedAt;
};

// Una "tarea" es un evento cuyo título empieza por "[T]": ese prefijo es el
// marcador visible también en Google Calendar (el título se sincroniza tal
// cual). Una tarea lleva asignatura asociada y su inicio es la fecha de entrega.
inline const QString kTaskTitlePrefix = QStringLiteral("[T]");

inline bool isTask(const CalendarEvent& event) {
    return event.title.startsWith(kTaskTitlePrefix);
}

// Título sin el prefijo "[T]", para mostrarlo limpio en la UI.
inline QString taskDisplayTitle(const CalendarEvent& event) {
    return isTask(event) ? event.title.mid(kTaskTitlePrefix.size()).trimmed() : event.title;
}

} // namespace pass

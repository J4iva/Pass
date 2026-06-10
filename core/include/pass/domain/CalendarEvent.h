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
    QString title;
    QString description;
    QDateTime startUtc; // siempre UTC
    QDateTime endUtc;   // siempre UTC
    bool allDay = false;
    QString rrule; // RRULE simplificada; vacía = sin recurrencia (MVP)
    QUuid subjectId;
    QUuid sourceSessionId; // sesión de estudio planificada que originó el evento
    QDateTime updatedAt;
};

} // namespace pass

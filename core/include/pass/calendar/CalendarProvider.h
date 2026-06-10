// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"

#include <QList>
#include <QObject>

namespace pass {

// Interfaz común para fuentes de calendario. El MVP solo tiene la local
// (SQLite); en fase 2 GoogleCalendarProvider implementará esta misma interfaz.
class CalendarProvider : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual QString providerId() const = 0;
    virtual QList<CalendarEvent> eventsBetween(const QDateTime& fromUtc,
                                               const QDateTime& toUtc) = 0;
    // Asigna id/updatedAt al evento si procede. Devuelve false si falla.
    virtual bool addEvent(CalendarEvent& event) = 0;
    virtual bool updateEvent(const CalendarEvent& event) = 0;
    virtual bool removeEvent(const QUuid& id) = 0;

signals:
    void eventsChanged();
};

} // namespace pass

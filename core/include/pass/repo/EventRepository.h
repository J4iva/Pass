// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"

#include <QList>
#include <QSqlDatabase>

#include <optional>

namespace pass {

class EventRepository {
public:
    explicit EventRepository(QSqlDatabase db);

    // Eventos que se solapan con [fromUtc, toUtc), ordenados por inicio.
    QList<CalendarEvent> between(const QDateTime& fromUtc, const QDateTime& toUtc) const;
    std::optional<CalendarEvent> byId(const QUuid& id) const;
    bool add(const CalendarEvent& event);
    bool update(const CalendarEvent& event);
    bool remove(const QUuid& id);

private:
    QSqlDatabase m_db;
};

} // namespace pass

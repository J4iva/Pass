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

    // Fila espejo de un evento remoto, por (provider, external_id).
    std::optional<CalendarEvent> byExternalId(const QString& provider,
                                              const QString& externalId) const;
    // Inserta o actualiza la fila espejo identificada por (provider, external_id).
    // Conserva el id local si ya existía; si no, asigna uno nuevo. Devuelve false
    // si falla. No emite señales (eso es responsabilidad del provider/servicio).
    bool upsertByExternalId(const CalendarEvent& event);
    bool removeByExternalId(const QString& provider, const QString& externalId);

private:
    QSqlDatabase m_db;
};

} // namespace pass

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/StudySession.h"

#include <QList>
#include <QSqlDatabase>

#include <optional>

namespace pass {

class SessionRepository {
public:
    explicit SessionRepository(QSqlDatabase db);

    QList<StudySession> all() const; // más recientes primero
    std::optional<StudySession> byId(const QUuid& id) const;
    bool add(const StudySession& session);
    bool update(const StudySession& session);
    bool remove(const QUuid& id);

    // Segundos trabajados en total en las sesiones vinculadas a un evento
    // (p. ej. una tarea). Suma actual_sec de las filas con ese event_id.
    qint64 totalSecondsForEvent(const QUuid& eventId) const;

private:
    QSqlDatabase m_db;
};

} // namespace pass

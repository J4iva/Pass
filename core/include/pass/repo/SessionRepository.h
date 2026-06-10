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

private:
    QSqlDatabase m_db;
};

} // namespace pass

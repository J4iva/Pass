// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/Topic.h"

#include <QList>
#include <QSqlDatabase>

#include <optional>

namespace pass {

class TopicRepository {
public:
    explicit TopicRepository(QSqlDatabase db);

    QList<Topic> bySubject(const QUuid& subjectId) const; // ordenados por nombre
    std::optional<Topic> byId(const QUuid& id) const;
    // Resolución por (asignatura, nombre) exacto, consistente con UNIQUE(subject_id,name).
    std::optional<Topic> bySubjectAndName(const QUuid& subjectId, const QString& name) const;
    bool add(const Topic& topic);
    bool update(const Topic& topic);
    bool remove(const QUuid& id);

private:
    QSqlDatabase m_db;
};

} // namespace pass

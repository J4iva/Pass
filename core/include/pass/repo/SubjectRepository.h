// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/Subject.h"

#include <QList>
#include <QSqlDatabase>

#include <optional>

namespace pass {

class SubjectRepository {
public:
    explicit SubjectRepository(QSqlDatabase db);

    QList<Subject> all(bool includeArchived = false) const;
    std::optional<Subject> byId(const QUuid& id) const;
    bool add(const Subject& subject);
    bool update(const Subject& subject);
    bool remove(const QUuid& id);

private:
    QSqlDatabase m_db;
};

} // namespace pass

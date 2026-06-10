// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/PomodoroStrategy.h"

#include <QList>
#include <QSqlDatabase>

#include <optional>

namespace pass {

class StrategyRepository {
public:
    explicit StrategyRepository(QSqlDatabase db);

    QList<PomodoroStrategy> all() const;
    std::optional<PomodoroStrategy> byId(const QUuid& id) const;
    bool add(const PomodoroStrategy& strategy);
    // Solo elimina estrategias personalizadas; las builtin son intocables.
    bool remove(const QUuid& id);

private:
    QSqlDatabase m_db;
};

} // namespace pass

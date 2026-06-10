// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/repo/StrategyRepository.h"

#include <QSqlQuery>

namespace pass {

namespace {

PomodoroStrategy fromRow(const QSqlQuery& q) {
    PomodoroStrategy s;
    s.id = QUuid::fromString(q.value(0).toString());
    s.name = q.value(1).toString();
    s.workMinutes = q.value(2).toInt();
    s.breakMinutes = q.value(3).toInt();
    s.longBreakMinutes = q.value(4).toInt();
    s.cyclesBeforeLongBreak = q.value(5).toInt();
    s.builtin = q.value(6).toBool();
    return s;
}

constexpr auto kColumns = "id, name, work_min, break_min, long_break_min, "
                          "cycles_before_long, builtin";

} // namespace

StrategyRepository::StrategyRepository(QSqlDatabase db) : m_db(std::move(db)) {}

QList<PomodoroStrategy> StrategyRepository::all() const {
    QList<PomodoroStrategy> result;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM strategies ORDER BY builtin DESC, work_min")
                    .arg(QLatin1String(kColumns))))
        return result;
    while (q.next())
        result.append(fromRow(q));
    return result;
}

std::optional<PomodoroStrategy> StrategyRepository::byId(const QUuid& id) const {
    QSqlQuery q(m_db);
    q.prepare(
        QStringLiteral("SELECT %1 FROM strategies WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    if (!q.exec() || !q.next())
        return std::nullopt;
    return fromRow(q);
}

bool StrategyRepository::add(const PomodoroStrategy& strategy) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO strategies(id, name, work_min, break_min, "
                             "long_break_min, cycles_before_long, builtin) "
                             "VALUES(?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(strategy.id.toString(QUuid::WithoutBraces));
    q.addBindValue(strategy.name);
    q.addBindValue(strategy.workMinutes);
    q.addBindValue(strategy.breakMinutes);
    q.addBindValue(strategy.longBreakMinutes);
    q.addBindValue(strategy.cyclesBeforeLongBreak);
    q.addBindValue(strategy.builtin ? 1 : 0);
    return q.exec();
}

bool StrategyRepository::remove(const QUuid& id) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM strategies WHERE id = ? AND builtin = 0"));
    q.addBindValue(id.toString(QUuid::WithoutBraces));
    return q.exec() && q.numRowsAffected() == 1;
}

} // namespace pass

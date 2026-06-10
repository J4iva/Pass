// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/session/StrategyCatalog.h"

#include <algorithm>

namespace pass::StrategyCatalog {

namespace {

// Descanso que toca tras el bloque de trabajo `workIndex` (1-based).
int breakAfter(const PomodoroStrategy& s, int workIndex) {
    if (s.cyclesBeforeLongBreak > 0 && workIndex % s.cyclesBeforeLongBreak == 0)
        return s.longBreakMinutes;
    return s.breakMinutes;
}

} // namespace

QList<SessionPlan> proposals(int targetMinutes, const QList<PomodoroStrategy>& strategies) {
    QList<SessionPlan> plans;
    for (const auto& s : strategies) {
        if (s.workMinutes <= 0 || s.workMinutes > targetMinutes)
            continue;

        // Añade ciclos mientras quepan: el descanso solo cuenta si después
        // viene otro bloque de trabajo.
        int cycles = 1;
        int total = s.workMinutes;
        while (true) {
            const int next = breakAfter(s, cycles) + s.workMinutes;
            if (total + next > targetMinutes)
                break;
            total += next;
            ++cycles;
        }

        plans.append({s, cycles, cycles * s.workMinutes, total});
    }

    std::stable_sort(plans.begin(), plans.end(), [](const SessionPlan& a, const SessionPlan& b) {
        return a.totalWorkMinutes > b.totalWorkMinutes;
    });
    return plans;
}

QString describe(const SessionPlan& plan) {
    return QStringLiteral("%1×%2 + descansos de %3 → %4 min (%5 de trabajo)")
        .arg(plan.cycles)
        .arg(plan.strategy.workMinutes)
        .arg(plan.strategy.breakMinutes)
        .arg(plan.totalMinutes)
        .arg(plan.totalWorkMinutes);
}

} // namespace pass::StrategyCatalog

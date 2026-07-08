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
        //
        // Defensa en profundidad (CWE-835): un descanso negativo de una
        // estrategia corrupta o sincronizada haría `next <= 0`, `total` dejaría
        // de crecer y el bucle no terminaría nunca, congelando la GUI. Exigimos
        // `next > 0` para progresar y acotamos los ciclos con un tope duro, de
        // modo que ningún dato de entrada pueda colgar el cálculo aunque la
        // validación previa (SyncSerializer) fallara.
        constexpr int kMaxCycles = 1000; // ninguna sesión real se acerca
        int cycles = 1;
        int total = s.workMinutes;
        while (cycles < kMaxCycles) {
            const int next = breakAfter(s, cycles) + s.workMinutes;
            if (next <= 0 || total + next > targetMinutes)
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

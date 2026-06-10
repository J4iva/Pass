// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/PomodoroStrategy.h"

#include <QList>
#include <QString>

namespace pass {

// Un plan concreto de sesión: N ciclos de una estrategia que encajan en el
// tiempo objetivo del usuario, p. ej. "2×(50+10)" para 120 minutos.
struct SessionPlan {
    PomodoroStrategy strategy;
    int cycles = 0;           // bloques de trabajo
    int totalWorkMinutes = 0; // solo trabajo
    int totalMinutes = 0;     // trabajo + descansos (sin descanso tras el último bloque)
};

// Funciones puras: fáciles de testear sin base de datos ni UI.
namespace StrategyCatalog {

// Propone, para cada estrategia que quepa, el plan con más ciclos que no
// supere targetMinutes. Ordenados por minutos de trabajo efectivo (desc).
QList<SessionPlan> proposals(int targetMinutes, const QList<PomodoroStrategy>& strategies);

// "4×25 + descansos de 5 → 115 min (100 de trabajo)"
QString describe(const SessionPlan& plan);

} // namespace StrategyCatalog

} // namespace pass

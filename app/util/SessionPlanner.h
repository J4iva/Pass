// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/session/StrategyCatalog.h"

#include <QDateTime>
#include <QString>
#include <QUuid>

namespace util {

enum class PlanStatus {
    Ok,             // sesión y evento creados
    CalendarFailed, // no se pudo crear el evento de calendario
    SaveFailed,     // el evento se creó pero la sesión no se pudo guardar
};

// Planifica una sesión: crea la StudySession (estado Planned) y su evento de
// calendario asociado, a partir de un plan elegido y la hora local de inicio.
// Si la sesión se asignó a una tarea, sus horas se vinculan a ella
// (`linkedEventId`); si no, al evento de planificación creado.
PlanStatus planSession(pass::SubjectRepository& subjects, pass::SessionRepository& sessions,
                       pass::CalendarProvider& calendar, const pass::SessionPlan& plan,
                       const QDateTime& startLocal, const QUuid& subjectId, const QString& topic,
                       const QUuid& taskId);

} // namespace util

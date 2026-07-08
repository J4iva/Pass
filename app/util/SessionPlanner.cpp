// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionPlanner.h"

#include "pass/domain/CalendarEvent.h"
#include "pass/domain/StudySession.h"

#include <QObject>

using namespace pass;

namespace util {

PlanStatus planSession(SubjectRepository& subjects, SessionRepository& sessions,
                       CalendarProvider& calendar, const SessionPlan& plan,
                       const QDateTime& startLocal, const QUuid& subjectId, const QString& topic,
                       const QUuid& taskId) {
    StudySession session;
    session.id = QUuid::createUuid();
    session.subjectId = subjectId;
    session.strategyId = plan.strategy.id;
    session.topic = topic;
    session.plannedMinutes = plan.totalMinutes;
    session.startedAt = startLocal.toUTC();
    session.status = SessionStatus::Planned;

    QString subjectName;
    if (const auto subject = subjects.byId(subjectId))
        subjectName = subject->name;
    CalendarEvent event;
    event.title = subjectName.isEmpty()
                      ? (topic.isEmpty() ? QObject::tr("Sesión de trabajo")
                                         : QObject::tr("Sesión: %1").arg(topic))
                      : QObject::tr("Sesión: %1").arg(subjectName);
    event.description = topic;
    event.startUtc = startLocal.toUTC();
    event.endUtc = startLocal.addSecs(qint64(plan.totalMinutes) * 60).toUTC();
    event.subjectId = subjectId;
    event.sourceSessionId = session.id;

    if (!calendar.addEvent(event))
        return PlanStatus::CalendarFailed;

    // Si la sesión se asignó a una tarea, el vínculo apunta a la tarea (sus
    // horas cuentan para ella); si no, al evento de planificación creado.
    session.linkedEventId = taskId.isNull() ? event.id : taskId;
    return sessions.add(session) ? PlanStatus::Ok : PlanStatus::SaveFailed;
}

} // namespace util

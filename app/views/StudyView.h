// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"
#include "pass/session/SessionTimerService.h"

#include <QWidget>

#include <optional>

class QLabel;
class QListWidget;
class QPushButton;
class TimerWidget;

class StudyView : public QWidget {
    Q_OBJECT

public:
    StudyView(pass::Database& db, pass::SessionTimerService* timer,
              pass::CalendarProvider* calendar, QWidget* parent = nullptr);

private:
    void startNewSession();
    void planSession(const pass::SessionPlan& plan, const QDateTime& startLocal,
                     const QUuid& subjectId, const QString& topic, const QUuid& taskId);
    // Lanza una sesión pendiente (planificada o interrumpida) elegida en la lista.
    void startPlannedSession(const pass::StudySession& planned);
    void startSelectedPlanned();
    void refreshPlanned(); // recarga la lista de sesiones pendientes (planificadas + retomables)
    void onFinished(const pass::StudySession& session);
    // Fases reconstruidas de una sesión (estrategia + minutos; fallback bloque único).
    QList<pass::SessionTimerService::PhaseSpec> phasesForSession(const pass::StudySession& s) const;
    // Segundos que faltan para completar una sesión interrumpida retomable.
    int remainingSecondsFor(const pass::StudySession& s) const;

    pass::SubjectRepository m_subjects;
    pass::TopicRepository m_topics;
    pass::StrategyRepository m_strategies;
    pass::SessionRepository m_sessions;
    pass::EventRepository m_events;
    pass::SessionTimerService* m_timer;
    pass::CalendarProvider* m_calendar;
    std::optional<pass::StudySession> m_activePlanned;
    QUuid m_pendingTaskId; // tarea elegida para la sesión inmediata en curso
    QList<pass::StudySession> m_planned; // pendientes mostradas en la lista
    QLabel* m_status;
    QListWidget* m_plannedList;
    QPushButton* m_startPlanned;
};

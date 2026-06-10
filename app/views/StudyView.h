// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/session/SessionTimerService.h"

#include <QWidget>

#include <optional>

class QLabel;
class TimerWidget;

class StudyView : public QWidget {
    Q_OBJECT

public:
    StudyView(pass::Database& db, pass::SessionTimerService* timer,
              pass::CalendarProvider* calendar, QWidget* parent = nullptr);

    // Lanza una sesión previamente planificada (desde el calendario).
    void startPlannedSession(const pass::StudySession& planned);

private:
    void startNewSession();
    void planSession(const pass::SessionPlan& plan, const QDateTime& startLocal,
                     const QUuid& subjectId, const QString& topic);
    void onFinished(const pass::StudySession& session);

    pass::SubjectRepository m_subjects;
    pass::StrategyRepository m_strategies;
    pass::SessionRepository m_sessions;
    pass::SessionTimerService* m_timer;
    pass::CalendarProvider* m_calendar;
    std::optional<pass::StudySession> m_activePlanned;
    QLabel* m_status;
};

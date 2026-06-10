// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/db/Database.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/session/SessionTimerService.h"

#include <QWidget>

class QLabel;
class TimerWidget;

class StudyView : public QWidget {
    Q_OBJECT

public:
    StudyView(pass::Database& db, pass::SessionTimerService* timer, QWidget* parent = nullptr);

private:
    void startNewSession();
    void onFinished(const pass::StudySession& session);

    pass::SubjectRepository m_subjects;
    pass::StrategyRepository m_strategies;
    pass::SessionRepository m_sessions;
    pass::SessionTimerService* m_timer;
    QLabel* m_status;
};

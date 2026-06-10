// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/session/SessionTimerService.h"

#include <QWidget>

class QLabel;
class QPushButton;

// Cuenta atrás grande con fase actual y controles pausar/reanudar/terminar.
class TimerWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimerWidget(pass::SessionTimerService* service, QWidget* parent = nullptr);

signals:
    void newSessionRequested();

private:
    void onTick(int remainingSeconds, pass::SessionTimerService::Phase phase);
    void onStateChanged(pass::SessionTimerService::State state);
    void updatePhaseLabel(pass::SessionTimerService::Phase phase);

    pass::SessionTimerService* m_service;
    QLabel* m_time;
    QLabel* m_phase;
    QPushButton* m_newSession;
    QPushButton* m_pauseResume;
    QPushButton* m_stop;
};

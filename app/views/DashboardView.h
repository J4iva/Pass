// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/SessionRepository.h"
#include "pass/settings/AppSettings.h"
#include "pass/stats/StatsService.h"

#include <QWidget>

class QLabel;
class QListWidget;
class QSpinBox;

// Resumen de un vistazo: horas de la semana, próximos eventos (ventana de días
// configurable), tareas pendientes con urgencia y horas trabajadas, y última nota.
class DashboardView : public QWidget {
    Q_OBJECT

public:
    DashboardView(pass::Database& db, pass::CalendarProvider* calendar,
                  QWidget* parent = nullptr);

    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void refreshEvents();
    void refreshTasks();

    pass::StatsService m_stats;
    pass::SessionRepository m_sessions;
    pass::CalendarProvider* m_calendar;
    pass::AppSettings m_settings;

    QLabel* m_week;
    QSpinBox* m_days;
    QListWidget* m_events;
    QListWidget* m_tasks;
    QLabel* m_lastNote;
};

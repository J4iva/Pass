// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/settings/AppSettings.h"
#include "pass/stats/StatsService.h"

#include <QWidget>

class QLabel;
class QListWidget;

// Resumen de un vistazo: horas de la semana, próximos eventos y última nota.
class DashboardView : public QWidget {
    Q_OBJECT

public:
    DashboardView(pass::Database& db, pass::CalendarProvider* calendar,
                  QWidget* parent = nullptr);

    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    pass::StatsService m_stats;
    pass::CalendarProvider* m_calendar;
    pass::AppSettings m_settings;

    QLabel* m_week;
    QListWidget* m_events;
    QLabel* m_lastNote;
};

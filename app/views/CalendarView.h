// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/StrategyRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/repo/TopicRepository.h"

#include <QWidget>

class ActivityCalendarWidget;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class CalendarView : public QWidget {
    Q_OBJECT

public:
    CalendarView(pass::Database& db, pass::CalendarProvider* provider, QWidget* parent = nullptr);

    // Recarga la lista del día y el mapa de actividad (p. ej. tras una sync remota).
    void refresh();

private:
    void refreshDayList();
    void refreshActivity();
    QWidget* buildLegend();
    void newEvent(bool asTask);
    void newSession();
    void editEvent(QListWidgetItem* item);
    void deleteSelected();
    std::optional<pass::CalendarEvent> selectedEvent() const;

    pass::SubjectRepository m_subjects;
    pass::TopicRepository m_topics;
    pass::StrategyRepository m_strategies;
    pass::SessionRepository m_sessions;
    pass::CalendarProvider* m_provider;
    QList<pass::CalendarEvent> m_dayEvents;

    ActivityCalendarWidget* m_calendar;
    QListWidget* m_list;
    QPushButton* m_edit;
    QPushButton* m_delete;
};

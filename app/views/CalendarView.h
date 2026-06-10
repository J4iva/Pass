// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/SubjectRepository.h"

#include <QWidget>

class QCalendarWidget;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class CalendarView : public QWidget {
    Q_OBJECT

public:
    CalendarView(pass::Database& db, pass::CalendarProvider* provider, QWidget* parent = nullptr);

signals:
    // El usuario quiere empezar la sesión de estudio planificada de este evento.
    void startSessionRequested(const QUuid& sessionId);

private:
    void refreshDayList();
    void refreshMonthMarks();
    void newEvent();
    void editEvent(QListWidgetItem* item);
    void deleteSelected();
    std::optional<pass::CalendarEvent> selectedEvent() const;

    pass::SubjectRepository m_subjects;
    pass::CalendarProvider* m_provider;
    QList<pass::CalendarEvent> m_dayEvents;

    QCalendarWidget* m_calendar;
    QListWidget* m_list;
    QPushButton* m_start;
    QPushButton* m_edit;
    QPushButton* m_delete;
};

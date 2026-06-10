// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/LocalCalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/session/SessionTimerService.h"

#include <QMainWindow>

#include <memory>

class QListWidget;
class QStackedWidget;
class StudyView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void addPage(const QString& title, QWidget* page);
    void startPlannedSession(const QUuid& sessionId);

    std::unique_ptr<pass::Database> m_db;
    pass::SessionTimerService* m_timer;          // hijo de this (ownership Qt)
    pass::LocalCalendarProvider* m_calendar = nullptr; // ídem; nulo si la DB falló
    StudyView* m_studyView = nullptr;
    int m_studyPageIndex = -1;
    QListWidget* m_sidebar;
    QStackedWidget* m_pages;
};

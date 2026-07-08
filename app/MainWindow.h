// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarService.h"
#include "pass/db/Database.h"
#include "pass/google/GoogleAuthService.h"
#include "pass/google/GoogleCalendarClient.h"
#include "pass/google/GoogleSyncService.h"
#include "pass/google/WinCredTokenStore.h"
#include "pass/session/SessionTimerService.h"
#include "pass/settings/AppSettings.h"
#include "pass/sync/GitSyncController.h"

#include <QMainWindow>

#include <memory>

class CalendarView;
class DashboardView;
class QListWidget;
class QStackedWidget;
class StatsView;
class StudyView;

namespace pass {
class VaultWatcher;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void addPage(const QString& title, QWidget* page);
    void refreshDataViews(); // tras aplicar datos remotos sincronizados

    std::unique_ptr<pass::Database> m_db;
    std::unique_ptr<pass::WinCredTokenStore> m_tokenStore;
    pass::AppSettings m_settings;
    pass::SessionTimerService* m_timer;            // hijo de this (ownership Qt)
    pass::GoogleAuthService* m_auth = nullptr;     // ídem (ownership Qt)
    pass::GoogleCalendarClient* m_client = nullptr;
    pass::GoogleSyncService* m_sync = nullptr;
    pass::sync::GitSyncController* m_gitSync = nullptr; // sync entre dispositivos (hilo worker)
    pass::VaultWatcher* m_notesWatcher = nullptr;  // dispara push si cambian las notas
    pass::CalendarService* m_calendar = nullptr;   // router; nulo si la DB falló
    DashboardView* m_dashboardView = nullptr;
    CalendarView* m_calendarView = nullptr;
    StatsView* m_statsView = nullptr;
    StudyView* m_studyView = nullptr;
    QListWidget* m_sidebar;
    QStackedWidget* m_pages;
};

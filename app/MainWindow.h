// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/db/Database.h"
#include "pass/session/SessionTimerService.h"

#include <QMainWindow>

#include <memory>

class QListWidget;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void addPage(const QString& title, QWidget* page);

    std::unique_ptr<pass::Database> m_db;
    pass::SessionTimerService* m_timer; // hijo de this (ownership Qt)
    QListWidget* m_sidebar;
    QStackedWidget* m_pages;
};

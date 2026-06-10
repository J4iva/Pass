// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/db/Database.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/stats/StatsService.h"

#include <QWidget>

class QLabel;
class QChartView;
class SessionTableModel;

class StatsView : public QWidget {
    Q_OBJECT

public:
    explicit StatsView(pass::Database& db, QWidget* parent = nullptr);

    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void rebuildSubjectChart(const QList<pass::SubjectHours>& rows);
    void rebuildDailyChart(const QList<pass::DailyMinutes>& series);

    pass::StatsService m_stats;
    pass::SessionRepository m_sessions;
    pass::SubjectRepository m_subjects;

    QLabel* m_summary;
    QChartView* m_subjectChart;
    QChartView* m_dailyChart;
    SessionTableModel* m_model;
};

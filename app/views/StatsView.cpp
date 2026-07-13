// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatsView.h"

#include "../models/SessionTableModel.h"
#include "../theme/Theme.h"

#include <QAbstractAxis>
#include <QBarCategoryAxis>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QDateTimeAxis>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHorizontalBarSeries>
#include <QLabel>
#include <QLineSeries>
#include <QPen>
#include <QTableView>
#include <QValueAxis>
#include <QVBoxLayout>

using namespace pass;

namespace {

// Tema monocromo para QChart: fondo transparente (se funde con la vista),
// titulo a fosforo, ejes tenues y rejilla casi invisible. Las series se
// colorean a mano (Qt Charts respeta poco el QSS y ChartThemeDark trae color).
void applyChartTheme(QChart* chart) {
    chart->setTheme(QChart::ChartThemeDark);
    // Fondo del chart y del plot area a la fuerza oscuros (Qt Charts deja blanco
    // el de barras por defecto y respeta poco el QSS).
    chart->setBackgroundVisible(true);
    chart->setBackgroundBrush(QBrush(theme::kBg));
    chart->setBackgroundPen(Qt::NoPen);
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(QBrush(theme::kBg));
    chart->setPlotAreaBackgroundPen(Qt::NoPen);
    chart->setTitleBrush(theme::kFg);
    chart->setTitleFont(theme::bodyFont(11));
    chart->legend()->hide();
    QFont f = theme::bodyFont(10);
    chart->setFont(f);
}

void styleAxis(QAbstractAxis* axis) {
    axis->setLabelsColor(theme::kFgDim);
    axis->setLinePenColor(theme::kRule);
    axis->setLabelsFont(theme::bodyFont(8));
    axis->setGridLineVisible(true);
    if (auto* va = qobject_cast<QValueAxis*>(axis))
        va->setGridLineColor(theme::kRule);
    else if (auto* da = qobject_cast<QDateTimeAxis*>(axis))
        da->setGridLineColor(theme::kRule);
    else if (auto* ca = qobject_cast<QBarCategoryAxis*>(axis))
        ca->setGridLineColor(theme::kRule);
}

} // namespace

StatsView::StatsView(Database& db, QWidget* parent)
    : QWidget(parent), m_stats(db.handle()), m_sessions(db.handle()), m_subjects(db.handle()),
      m_summary(new QLabel), m_subjectChart(new QChartView), m_dailyChart(new QChartView),
      m_model(new SessionTableModel(this)) {
    m_subjectChart->setRenderHint(QPainter::Antialiasing);
    m_dailyChart->setRenderHint(QPainter::Antialiasing);
    m_subjectChart->setBackgroundBrush(QBrush(theme::kBg));
    m_dailyChart->setBackgroundBrush(QBrush(theme::kBg));

    auto* table = new QTableView;
    table->setModel(m_model);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* charts = new QHBoxLayout;
    charts->addWidget(m_subjectChart);
    charts->addWidget(m_dailyChart);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(charts, 2);
    layout->addWidget(table, 1);
}

void StatsView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();
}

void StatsView::refresh() {
    const qint64 totalSec = m_stats.totalWorkSeconds();
    m_summary->setText(tr("<b>%1 h %2 min</b> de trabajo en <b>%3</b> sesiones completadas")
                           .arg(totalSec / 3600)
                           .arg((totalSec % 3600) / 60)
                           .arg(m_stats.completedSessionCount()));

    rebuildSubjectChart(m_stats.hoursBySubject());
    rebuildDailyChart(m_stats.minutesPerDay(30));

    QHash<QUuid, QString> names;
    for (const auto& s : m_subjects.all(/*includeArchived=*/true))
        names.insert(s.id, s.name);
    m_model->reload(m_sessions.all(), names);
}

void StatsView::rebuildSubjectChart(const QList<SubjectHours>& rows) {
    auto* set = new QBarSet(tr("Horas"));
    QStringList categories;
    // Invertido: en barras horizontales la primera categoría queda abajo.
    for (auto it = rows.crbegin(); it != rows.crend(); ++it) {
        *set << double(it->workSeconds) / 3600.0;
        categories << (it->subjectName.isEmpty() ? tr("(sin asignatura)") : it->subjectName);
    }

    auto* series = new QHorizontalBarSeries;
    series->append(set);
    set->setColor(theme::kFg);
    set->setBorderColor(theme::kBg);

    auto* chart = new QChart;
    applyChartTheme(chart);
    chart->addSeries(series);
    chart->setTitle(tr("Horas por asignatura"));

    auto* axisY = new QBarCategoryAxis;
    axisY->append(categories);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    auto* axisX = new QValueAxis;
    axisX->setLabelFormat(QStringLiteral("%.1f"));
    axisX->applyNiceNumbers();
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    styleAxis(axisY);
    styleAxis(axisX);

    QChart* old = m_subjectChart->chart();
    m_subjectChart->setChart(chart);
    delete old;
}

void StatsView::rebuildDailyChart(const QList<DailyMinutes>& series) {
    auto* line = new QLineSeries;
    int maxMinutes = 0;
    for (const auto& day : series) {
        line->append(QDateTime(day.date, QTime(12, 0)).toMSecsSinceEpoch(), day.minutes);
        maxMinutes = qMax(maxMinutes, day.minutes);
    }

    auto* chart = new QChart;
    applyChartTheme(chart);
    chart->addSeries(line);
    chart->setTitle(tr("Minutos de trabajo (últimos 30 días)"));

    auto* axisX = new QDateTimeAxis;
    axisX->setFormat(QStringLiteral("dd/MM"));
    axisX->setTickCount(7);
    chart->addAxis(axisX, Qt::AlignBottom);
    line->attachAxis(axisX);

    auto* axisY = new QValueAxis;
    axisY->setRange(0, qMax(60, maxMinutes + 10));
    axisY->setLabelFormat(QStringLiteral("%d"));
    chart->addAxis(axisY, Qt::AlignLeft);
    line->attachAxis(axisY);

    line->setPen(QPen(theme::kFg, 2));
    line->setColor(theme::kFg);
    styleAxis(axisX);
    styleAxis(axisY);

    QChart* old = m_dailyChart->chart();
    m_dailyChart->setChart(chart);
    delete old;
}

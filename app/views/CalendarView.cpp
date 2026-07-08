// SPDX-License-Identifier: GPL-3.0-or-later
#include "CalendarView.h"

#include "../util/SessionPlanner.h"
#include "../widgets/ActivityCalendarWidget.h"
#include "../widgets/EventDialog.h"
#include "../widgets/SessionSetupDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

using namespace pass;

namespace {

// Ventana UTC que cubre el día local `date` completo.
QPair<QDateTime, QDateTime> dayWindowUtc(const QDate& date) {
    return {QDateTime(date, QTime(0, 0)).toUTC(), QDateTime(date.addDays(1), QTime(0, 0)).toUTC()};
}

// A qué tipo de actividad pertenece un evento (prioridad: estudio > google > local).
ActivityCalendarWidget::Category categoryOf(const CalendarEvent& e) {
    if (!e.sourceSessionId.isNull())
        return ActivityCalendarWidget::Study;
    if (e.provider == QStringLiteral("google"))
        return ActivityCalendarWidget::Google;
    return ActivityCalendarWidget::Local;
}

// Una muestra de color (círculo relleno) para la leyenda.
QPixmap legendDot(const QColor& color) {
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(1, 1, 10, 10);
    return pm;
}

} // namespace

CalendarView::CalendarView(Database& db, CalendarProvider* provider, QWidget* parent)
    : QWidget(parent), m_subjects(db.handle()), m_topics(db.handle()), m_strategies(db.handle()),
      m_sessions(db.handle()), m_provider(provider), m_calendar(new ActivityCalendarWidget),
      m_list(new QListWidget),
      m_edit(new QPushButton(tr("Editar"))), m_delete(new QPushButton(tr("Eliminar"))) {
    m_calendar->setGridVisible(true);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);

    auto* newButton = new QPushButton(tr("Nuevo evento"));
    auto* newTaskButton = new QPushButton(tr("Nueva tarea"));
    auto* newSessionButton = new QPushButton(tr("Nueva sesión"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(newTaskButton);
    buttons->addWidget(newSessionButton);
    buttons->addWidget(m_edit);
    buttons->addWidget(m_delete);
    buttons->addStretch();

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Eventos del día:")));
    right->addWidget(m_list, 1);
    right->addLayout(buttons);

    auto* left = new QVBoxLayout;
    left->addWidget(m_calendar, 1);
    left->addWidget(buildLegend());

    auto* layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addLayout(right, 1);

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &CalendarView::refreshDayList);
    connect(m_calendar, &QCalendarWidget::currentPageChanged, this,
            [this](int, int) { refreshActivity(); });
    connect(m_provider, &CalendarProvider::eventsChanged, this, [this] {
        refreshDayList();
        refreshActivity();
    });
    connect(newButton, &QPushButton::clicked, this, [this] { newEvent(/*asTask=*/false); });
    connect(newTaskButton, &QPushButton::clicked, this, [this] { newEvent(/*asTask=*/true); });
    connect(newSessionButton, &QPushButton::clicked, this, &CalendarView::newSession);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &CalendarView::editEvent);
    connect(m_edit, &QPushButton::clicked, this, [this] {
        if (auto* item = m_list->currentItem())
            editEvent(item);
    });
    connect(m_delete, &QPushButton::clicked, this, &CalendarView::deleteSelected);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool valid = row >= 0 && row < m_dayEvents.size();
        m_edit->setEnabled(valid);
        m_delete->setEnabled(valid);
    });

    refreshDayList();
    refreshActivity();
}

void CalendarView::refresh() {
    refreshDayList();
    refreshActivity();
}

void CalendarView::refreshDayList() {
    const auto [from, to] = dayWindowUtc(m_calendar->selectedDate());
    m_dayEvents = m_provider->eventsBetween(from, to);

    m_list->clear();
    for (const auto& e : m_dayEvents) {
        const QString when = e.allDay
                                 ? tr("Todo el día")
                                 : QStringLiteral("%1–%2").arg(
                                       e.startUtc.toLocalTime().toString(QStringLiteral("HH:mm")),
                                       e.endUtc.toLocalTime().toString(QStringLiteral("HH:mm")));
        QString text = QStringLiteral("%1  %2").arg(when, taskDisplayTitle(e));
        if (isTask(e))
            text.prepend(QStringLiteral("📋 "));
        if (!e.sourceSessionId.isNull())
            text.prepend(QStringLiteral("📚 "));
        if (e.provider == QStringLiteral("google"))
            text.prepend(QStringLiteral("🌐 "));
        m_list->addItem(text);
    }
    m_edit->setEnabled(false);
    m_delete->setEnabled(false);
}

void CalendarView::refreshActivity() {
    // Mira un mes con margen a ambos lados, porque la rejilla del calendario
    // muestra días del mes anterior y siguiente.
    const QDate first(m_calendar->yearShown(), m_calendar->monthShown(), 1);
    const auto fromUtc = QDateTime(first.addDays(-7), QTime(0, 0)).toUTC();
    const auto toUtc = QDateTime(first.addMonths(1).addDays(7), QTime(0, 0)).toUTC();

    QHash<QDate, ActivityCalendarWidget::Categories> activity;
    for (const auto& e : m_provider->eventsBetween(fromUtc, toUtc)) {
        const auto category = categoryOf(e);
        for (QDate d = e.startUtc.toLocalTime().date(); d <= e.endUtc.toLocalTime().date();
             d = d.addDays(1)) {
            if (e.allDay && d == e.endUtc.toLocalTime().date())
                break; // el fin exclusivo de un all-day no marca el día siguiente
            activity[d] |= category;
        }
    }
    m_calendar->setActivity(activity);
}

QWidget* CalendarView::buildLegend() {
    struct Entry {
        ActivityCalendarWidget::Category category;
        QString label;
    };
    const Entry entries[] = {{ActivityCalendarWidget::Study, tr("Sesión")},
                             {ActivityCalendarWidget::Google, tr("Google")},
                             {ActivityCalendarWidget::Local, tr("Evento")}};

    auto* legend = new QWidget;
    auto* row = new QHBoxLayout(legend);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(14);
    for (const auto& e : entries) {
        auto* dot = new QLabel;
        dot->setPixmap(legendDot(ActivityCalendarWidget::categoryColor(e.category)));
        auto* text = new QLabel(e.label);
        auto* item = new QHBoxLayout;
        item->setSpacing(5);
        item->addWidget(dot);
        item->addWidget(text);
        row->addLayout(item);
    }
    row->addStretch();
    return legend;
}

void CalendarView::newEvent(bool asTask) {
    EventDialog dialog(m_subjects, m_topics, m_calendar->selectedDate(),
                       m_provider->canUploadToRemote(), this);
    dialog.setTaskMode(asTask);
    if (dialog.exec() != QDialog::Accepted)
        return;
    CalendarEvent e = dialog.result();
    if (!m_provider->addEvent(e))
        QMessageBox::warning(this, tr("Calendario"), tr("No se pudo guardar el evento."));
}

void CalendarView::newSession() {
    // Tareas pendientes (próximo año) a las que se puede asignar la sesión.
    const auto now = QDateTime::currentDateTimeUtc();
    QList<CalendarEvent> tasks;
    for (const auto& e : m_provider->eventsBetween(now, now.addDays(365))) {
        if (isTask(e))
            tasks.append(e);
    }

    // En el calendario solo se planifica (no se empieza): diálogo en modo
    // "solo planificar", con la fecha del día seleccionado.
    SessionSetupDialog dialog(m_subjects, m_topics, m_strategies, tasks, this);
    dialog.setPlanOnly(m_calendar->selectedDate());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto plan = dialog.selectedPlan();
    const auto when = dialog.plannedStart();
    if (!plan || !when)
        return;

    if (util::planSession(m_subjects, m_sessions, *m_provider, *plan, *when,
                          dialog.resolveSubjectId(), dialog.topic(),
                          dialog.selectedTaskId()) != util::PlanStatus::Ok) {
        QMessageBox::warning(this, tr("Calendario"), tr("No se pudo planificar la sesión."));
    }
}

void CalendarView::editEvent(QListWidgetItem* item) {
    const int row = m_list->row(item);
    if (row < 0 || row >= m_dayEvents.size())
        return;
    // En edición no se ofrece el check de Google (solo aplica a eventos nuevos).
    EventDialog dialog(m_subjects, m_topics, m_calendar->selectedDate(),
                       /*offerGoogleUpload=*/false, this);
    dialog.loadEvent(m_dayEvents[row]);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!m_provider->updateEvent(dialog.result()))
        QMessageBox::warning(this, tr("Calendario"), tr("No se pudo actualizar el evento."));
}

void CalendarView::deleteSelected() {
    const auto event = selectedEvent();
    if (!event)
        return;
    QString question = tr("¿Eliminar \"%1\"?").arg(event->title);
    if (event->provider == QStringLiteral("google"))
        question += tr("\n\nSe eliminará también de tu Google Calendar.");
    if (QMessageBox::question(this, tr("Calendario"), question) != QMessageBox::Yes)
        return;
    m_provider->removeEvent(event->id);
}

std::optional<CalendarEvent> CalendarView::selectedEvent() const {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_dayEvents.size())
        return std::nullopt;
    return m_dayEvents[row];
}

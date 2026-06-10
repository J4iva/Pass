// SPDX-License-Identifier: GPL-3.0-or-later
#include "CalendarView.h"

#include "../widgets/EventDialog.h"

#include <QCalendarWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTextCharFormat>
#include <QTimeZone>
#include <QVBoxLayout>

using namespace pass;

namespace {

// Ventana UTC que cubre el día local `date` completo.
QPair<QDateTime, QDateTime> dayWindowUtc(const QDate& date) {
    return {QDateTime(date, QTime(0, 0)).toUTC(),
            QDateTime(date.addDays(1), QTime(0, 0)).toUTC()};
}

} // namespace

CalendarView::CalendarView(Database& db, CalendarProvider* provider, QWidget* parent)
    : QWidget(parent), m_subjects(db.handle()), m_provider(provider),
      m_calendar(new QCalendarWidget), m_list(new QListWidget),
      m_start(new QPushButton(tr("▶ Empezar sesión"))), m_edit(new QPushButton(tr("Editar"))),
      m_delete(new QPushButton(tr("Eliminar"))) {
    m_calendar->setGridVisible(true);
    m_calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);

    auto* newButton = new QPushButton(tr("Nuevo evento"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(m_start);
    buttons->addWidget(m_edit);
    buttons->addWidget(m_delete);
    buttons->addStretch();

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("Eventos del día:")));
    right->addWidget(m_list, 1);
    right->addLayout(buttons);

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(m_calendar, 1);
    layout->addLayout(right, 1);

    connect(m_calendar, &QCalendarWidget::selectionChanged, this, &CalendarView::refreshDayList);
    connect(m_calendar, &QCalendarWidget::currentPageChanged, this,
            [this](int, int) { refreshMonthMarks(); });
    connect(m_provider, &CalendarProvider::eventsChanged, this, [this] {
        refreshDayList();
        refreshMonthMarks();
    });
    connect(newButton, &QPushButton::clicked, this, &CalendarView::newEvent);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &CalendarView::editEvent);
    connect(m_edit, &QPushButton::clicked, this, [this] {
        if (auto* item = m_list->currentItem())
            editEvent(item);
    });
    connect(m_delete, &QPushButton::clicked, this, &CalendarView::deleteSelected);
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (const auto event = selectedEvent(); event && !event->sourceSessionId.isNull())
            emit startSessionRequested(event->sourceSessionId);
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool valid = row >= 0 && row < m_dayEvents.size();
        m_edit->setEnabled(valid);
        m_delete->setEnabled(valid);
        m_start->setEnabled(valid && !m_dayEvents[row].sourceSessionId.isNull());
    });

    refreshDayList();
    refreshMonthMarks();
}

void CalendarView::refreshDayList() {
    const auto [from, to] = dayWindowUtc(m_calendar->selectedDate());
    m_dayEvents = m_provider->eventsBetween(from, to);

    m_list->clear();
    for (const auto& e : m_dayEvents) {
        const QString when =
            e.allDay ? tr("Todo el día")
                     : QStringLiteral("%1–%2").arg(
                           e.startUtc.toLocalTime().toString(QStringLiteral("HH:mm")),
                           e.endUtc.toLocalTime().toString(QStringLiteral("HH:mm")));
        QString text = QStringLiteral("%1  %2").arg(when, e.title);
        if (!e.sourceSessionId.isNull())
            text.prepend(QStringLiteral("📚 "));
        m_list->addItem(text);
    }
    m_edit->setEnabled(false);
    m_delete->setEnabled(false);
    m_start->setEnabled(false);
}

void CalendarView::refreshMonthMarks() {
    m_calendar->setDateTextFormat(QDate(), QTextCharFormat()); // limpia todo

    const QDate first(m_calendar->yearShown(), m_calendar->monthShown(), 1);
    const auto fromUtc = QDateTime(first, QTime(0, 0)).toUTC();
    const auto toUtc = QDateTime(first.addMonths(1), QTime(0, 0)).toUTC();

    QTextCharFormat marked;
    marked.setFontWeight(QFont::Bold);
    marked.setFontUnderline(true);
    for (const auto& e : m_provider->eventsBetween(fromUtc, toUtc)) {
        for (QDate d = e.startUtc.toLocalTime().date();
             d <= e.endUtc.toLocalTime().date() && d <= first.addMonths(1); d = d.addDays(1)) {
            if (e.allDay && d == e.endUtc.toLocalTime().date())
                break; // el fin exclusivo de un all-day no marca el día siguiente
            m_calendar->setDateTextFormat(d, marked);
        }
    }
}

void CalendarView::newEvent() {
    EventDialog dialog(m_subjects, m_calendar->selectedDate(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    CalendarEvent e = dialog.result();
    if (!m_provider->addEvent(e))
        QMessageBox::warning(this, tr("Calendario"), tr("No se pudo guardar el evento."));
}

void CalendarView::editEvent(QListWidgetItem* item) {
    const int row = m_list->row(item);
    if (row < 0 || row >= m_dayEvents.size())
        return;
    EventDialog dialog(m_subjects, m_calendar->selectedDate(), this);
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
    if (QMessageBox::question(this, tr("Calendario"),
                              tr("¿Eliminar \"%1\"?").arg(event->title)) != QMessageBox::Yes)
        return;
    m_provider->removeEvent(event->id);
}

std::optional<CalendarEvent> CalendarView::selectedEvent() const {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_dayEvents.size())
        return std::nullopt;
    return m_dayEvents[row];
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "DashboardView.h"

#include "pass/notes/VaultService.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace pass;

namespace {

// Urgencia de una tarea con UN solo color (ámbar, el de "Evento" en el
// calendario) variando solo la opacidad: más cerca de la entrega, más intenso.
QColor urgencyColor(int daysLeft) {
    QColor base(0xB7, 0x79, 0x1F);
    if (daysLeft <= 1)
        base.setAlpha(255);
    else if (daysLeft <= 3)
        base.setAlpha(190);
    else if (daysLeft <= 7)
        base.setAlpha(120);
    else
        base.setAlpha(60);
    return base;
}

QString dueText(int daysLeft) {
    if (daysLeft <= 0)
        return QObject::tr("entrega hoy");
    if (daysLeft == 1)
        return QObject::tr("entrega mañana");
    return QObject::tr("entrega en %1 días").arg(daysLeft);
}

QString workedText(qint64 seconds) {
    if (seconds <= 0)
        return QObject::tr("sin sesiones aún");
    const qint64 h = seconds / 3600;
    const qint64 min = (seconds % 3600) / 60;
    return QObject::tr("%1 h %2 min trabajados").arg(h).arg(min, 2, 10, QLatin1Char('0'));
}

} // namespace

DashboardView::DashboardView(Database& db, CalendarProvider* calendar, QWidget* parent)
    : QWidget(parent), m_stats(db.handle()), m_sessions(db.handle()), m_calendar(calendar),
      m_week(new QLabel), m_days(new QSpinBox), m_events(new QListWidget),
      m_tasks(new QListWidget), m_lastNote(new QLabel) {
    auto* title = new QLabel(tr("<h2>Tu semana</h2>"));
    m_week->setTextFormat(Qt::RichText);
    m_lastNote->setTextFormat(Qt::RichText);
    for (auto* list : {m_events, m_tasks}) {
        list->setSelectionMode(QAbstractItemView::NoSelection);
        list->setFocusPolicy(Qt::NoFocus);
    }

    // Ventana de próximos eventos configurable (se recuerda entre sesiones).
    m_days->setRange(1, 60);
    m_days->setValue(m_settings.dashboardDays());
    m_days->setSuffix(tr(" días"));
    m_days->setPrefix(tr("próximos "));
    connect(m_days, &QSpinBox::valueChanged, this, [this](int days) {
        m_settings.setDashboardDays(days);
        refreshEvents();
    });

    auto* eventsHeader = new QHBoxLayout;
    eventsHeader->addWidget(new QLabel(tr("<b>Eventos</b>")));
    eventsHeader->addStretch();
    eventsHeader->addWidget(m_days);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(title);
    layout->addWidget(m_week);
    layout->addSpacing(12);
    layout->addLayout(eventsHeader);
    layout->addWidget(m_events, 3);
    layout->addWidget(new QLabel(tr("<b>Tareas</b>")));
    layout->addWidget(m_tasks, 2);
    layout->addWidget(m_lastNote);

    if (m_calendar)
        connect(m_calendar, &CalendarProvider::eventsChanged, this, &DashboardView::refresh);
    refresh();
}

void DashboardView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();
}

void DashboardView::refresh() {
    int weekMinutes = 0;
    for (const auto& day : m_stats.minutesPerDay(7))
        weekMinutes += day.minutes;
    m_week->setText(tr("Has trabajado <b>%1 h %2 min</b> en los últimos 7 días "
                       "(%3 sesiones completadas en total).")
                        .arg(weekMinutes / 60)
                        .arg(weekMinutes % 60)
                        .arg(m_stats.completedSessionCount()));

    refreshEvents();
    refreshTasks();

    AppSettings settings;
    VaultService vault(settings.vaultPath(), settings.vaultSubfolder());
    const auto notes = vault.vaultExists() ? vault.notes() : QList<Note>{};
    m_lastNote->setText(
        notes.isEmpty()
            ? tr("Aún no hay notas en el vault.")
            : tr("Última nota: <b>%1</b> (%2)")
                  .arg(notes.first().title,
                       notes.first().modified.toString(QStringLiteral("dd/MM HH:mm"))));
}

void DashboardView::refreshEvents() {
    m_events->clear();
    if (!m_calendar)
        return;

    const auto now = QDateTime::currentDateTimeUtc();
    int shown = 0;
    for (const auto& e : m_calendar->eventsBetween(now, now.addDays(m_days->value()))) {
        if (isTask(e))
            continue; // las tareas tienen su propio apartado
        const QString when =
            e.allDay ? e.startUtc.toLocalTime().toString(QStringLiteral("ddd dd"))
                     : e.startUtc.toLocalTime().toString(QStringLiteral("ddd dd, HH:mm"));
        m_events->addItem(QStringLiteral("%1  —  %2").arg(when, e.title));
        ++shown;
    }
    if (shown == 0)
        m_events->addItem(tr("Sin eventos próximos. ¡Planifica una sesión de trabajo!"));
}

void DashboardView::refreshTasks() {
    m_tasks->clear();
    if (!m_calendar)
        return;

    // Las tareas se listan completas hacia el futuro (no limitadas por la
    // ventana de eventos): una entrega lejana también interesa verla venir.
    const auto now = QDateTime::currentDateTimeUtc();
    int shown = 0;
    for (const auto& e : m_calendar->eventsBetween(now, now.addDays(365))) {
        if (!isTask(e))
            continue;
        const int daysLeft = int(now.daysTo(e.startUtc));
        const qint64 worked = m_sessions.totalSecondsForEvent(e.id);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2 (%3)  ·  %4")
                .arg(taskDisplayTitle(e), dueText(daysLeft),
                     e.startUtc.toLocalTime().toString(QStringLiteral("dd/MM")),
                     workedText(worked)));
        item->setData(Qt::DecorationRole, urgencyColor(daysLeft));
        m_tasks->addItem(item);
        ++shown;
    }
    if (shown == 0)
        m_tasks->addItem(tr("Sin tareas pendientes."));
}

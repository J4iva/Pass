// SPDX-License-Identifier: GPL-3.0-or-later
#include "DashboardView.h"

#include "pass/notes/VaultService.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

using namespace pass;

DashboardView::DashboardView(Database& db, CalendarProvider* calendar, QWidget* parent)
    : QWidget(parent), m_stats(db.handle()), m_calendar(calendar), m_week(new QLabel),
      m_events(new QListWidget), m_lastNote(new QLabel) {
    auto* title = new QLabel(tr("<h2>Tu semana</h2>"));
    m_week->setTextFormat(Qt::RichText);
    m_lastNote->setTextFormat(Qt::RichText);
    m_events->setSelectionMode(QAbstractItemView::NoSelection);
    m_events->setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(title);
    layout->addWidget(m_week);
    layout->addSpacing(12);
    layout->addWidget(new QLabel(tr("<b>Próximos 7 días</b>")));
    layout->addWidget(m_events, 1);
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
    m_week->setText(tr("Has estudiado <b>%1 h %2 min</b> en los últimos 7 días "
                       "(%3 sesiones completadas en total).")
                        .arg(weekMinutes / 60)
                        .arg(weekMinutes % 60)
                        .arg(m_stats.completedSessionCount()));

    m_events->clear();
    if (m_calendar) {
        const auto now = QDateTime::currentDateTimeUtc();
        const auto events = m_calendar->eventsBetween(now, now.addDays(7));
        for (const auto& e : events) {
            const QString when =
                e.allDay ? e.startUtc.toLocalTime().toString(QStringLiteral("ddd dd"))
                         : e.startUtc.toLocalTime().toString(QStringLiteral("ddd dd, HH:mm"));
            m_events->addItem(QStringLiteral("%1  —  %2").arg(when, e.title));
        }
        if (events.isEmpty())
            m_events->addItem(tr("Sin eventos próximos. ¡Planifica una sesión de estudio!"));
    }

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

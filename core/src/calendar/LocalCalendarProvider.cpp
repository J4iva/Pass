// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/calendar/LocalCalendarProvider.h"

namespace pass {

LocalCalendarProvider::LocalCalendarProvider(QSqlDatabase db, QObject* parent)
    : CalendarProvider(parent), m_repo(std::move(db)) {}

QList<CalendarEvent> LocalCalendarProvider::eventsBetween(const QDateTime& fromUtc,
                                                          const QDateTime& toUtc) {
    return m_repo.between(fromUtc, toUtc);
}

bool LocalCalendarProvider::addEvent(CalendarEvent& event) {
    if (event.id.isNull())
        event.id = QUuid::createUuid();
    event.provider = providerId();
    if (!m_repo.add(event))
        return false;
    emit eventsChanged();
    return true;
}

bool LocalCalendarProvider::updateEvent(const CalendarEvent& event) {
    if (!m_repo.update(event))
        return false;
    emit eventsChanged();
    return true;
}

bool LocalCalendarProvider::removeEvent(const QUuid& id) {
    if (!m_repo.remove(id))
        return false;
    emit eventsChanged();
    return true;
}

} // namespace pass

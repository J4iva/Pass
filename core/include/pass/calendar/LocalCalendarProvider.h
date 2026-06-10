// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/repo/EventRepository.h"

namespace pass {

class LocalCalendarProvider : public CalendarProvider {
    Q_OBJECT

public:
    explicit LocalCalendarProvider(QSqlDatabase db, QObject* parent = nullptr);

    QString providerId() const override { return QStringLiteral("local"); }
    QList<CalendarEvent> eventsBetween(const QDateTime& fromUtc, const QDateTime& toUtc) override;
    bool addEvent(CalendarEvent& event) override;
    bool updateEvent(const CalendarEvent& event) override;
    bool removeEvent(const QUuid& id) override;

private:
    EventRepository m_repo;
};

} // namespace pass

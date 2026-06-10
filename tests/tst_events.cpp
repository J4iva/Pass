// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/calendar/LocalCalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/EventRepository.h"

#include <QSignalSpy>
#include <QtTest>

using namespace pass;

class EventsTest : public QObject {
    Q_OBJECT

private:
    CalendarEvent makeEvent(const QString& title, const QDateTime& startUtc, int minutes) {
        CalendarEvent e;
        e.id = QUuid::createUuid();
        e.title = title;
        e.startUtc = startUtc;
        e.endUtc = startUtc.addSecs(minutes * 60);
        return e;
    }

private slots:
    void crudAndOverlapQuery() {
        Database db(QStringLiteral(":memory:"));
        EventRepository repo(db.handle());

        const QDateTime base(QDate(2026, 6, 10), QTime(9, 0), QTimeZone::utc());
        QVERIFY(repo.add(makeEvent(QStringLiteral("Clase"), base, 60)));
        QVERIFY(repo.add(makeEvent(QStringLiteral("Examen"), base.addDays(1), 120)));

        // Solo el evento del día 10 solapa con esa ventana.
        const auto day10 = repo.between(base.addSecs(-3600), base.addSecs(2 * 3600));
        QCOMPARE(day10.size(), 1);
        QCOMPARE(day10[0].title, QStringLiteral("Clase"));

        auto fetched = repo.byId(day10[0].id);
        QVERIFY(fetched.has_value());
        fetched->title = QStringLiteral("Clase de álgebra");
        QVERIFY(repo.update(*fetched));
        QCOMPARE(repo.byId(fetched->id)->title, QStringLiteral("Clase de álgebra"));

        QVERIFY(repo.remove(fetched->id));
        QVERIFY(!repo.byId(fetched->id).has_value());
    }

    void providerEmitsEventsChanged() {
        Database db(QStringLiteral(":memory:"));
        LocalCalendarProvider provider(db.handle());
        QSignalSpy spy(&provider, &CalendarProvider::eventsChanged);

        CalendarEvent e = makeEvent(QStringLiteral("Repaso"),
                                    QDateTime::currentDateTimeUtc(), 30);
        e.id = QUuid(); // el provider debe asignar id
        QVERIFY(provider.addEvent(e));
        QVERIFY(!e.id.isNull());
        QCOMPARE(spy.count(), 1);

        e.title = QStringLiteral("Repaso tema 2");
        QVERIFY(provider.updateEvent(e));
        QCOMPARE(spy.count(), 2);

        QVERIFY(provider.removeEvent(e.id));
        QCOMPARE(spy.count(), 3);
        QVERIFY(!provider.removeEvent(e.id)); // ya no existe
        QCOMPARE(spy.count(), 3);
    }
};

QTEST_GUILESS_MAIN(EventsTest)
#include "tst_events.moc"

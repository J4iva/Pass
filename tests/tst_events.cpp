// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/calendar/LocalCalendarProvider.h"
#include "pass/db/Database.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SyncStateRepository.h"

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

    void mirrorUpsertByExternalId() {
        Database db(QStringLiteral(":memory:"));
        EventRepository repo(db.handle());

        const QDateTime base(QDate(2026, 6, 10), QTime(9, 0), QTimeZone::utc());
        CalendarEvent e = makeEvent(QStringLiteral("Reunión"), base, 30);
        e.id = QUuid(); // el upsert asigna id si no hay
        e.provider = QStringLiteral("google");
        e.externalId = QStringLiteral("EXT-1");
        e.etag = QStringLiteral("etag-v1");
        e.updatedAt = base; // se conserva, no se sobreescribe con "ahora"
        QVERIFY(repo.upsertByExternalId(e));

        auto fetched = repo.byExternalId(QStringLiteral("google"), QStringLiteral("EXT-1"));
        QVERIFY(fetched.has_value());
        QVERIFY(!fetched->id.isNull());
        QCOMPARE(fetched->etag, QStringLiteral("etag-v1"));
        QCOMPARE(fetched->updatedAt.toUTC(), base);
        const QUuid assignedId = fetched->id;

        // Segundo upsert: actualiza la misma fila (no duplica) y conserva el id.
        e.title = QStringLiteral("Reunión movida");
        e.etag = QStringLiteral("etag-v2");
        QVERIFY(repo.upsertByExternalId(e));
        fetched = repo.byExternalId(QStringLiteral("google"), QStringLiteral("EXT-1"));
        QVERIFY(fetched.has_value());
        QCOMPARE(fetched->id, assignedId);
        QCOMPARE(fetched->title, QStringLiteral("Reunión movida"));
        QCOMPARE(fetched->etag, QStringLiteral("etag-v2"));

        QVERIFY(repo.removeByExternalId(QStringLiteral("google"), QStringLiteral("EXT-1")));
        QVERIFY(!repo.byExternalId(QStringLiteral("google"), QStringLiteral("EXT-1")).has_value());
    }

    void syncStateKeyValue() {
        Database db(QStringLiteral(":memory:"));
        SyncStateRepository state(db.handle());

        QVERIFY(!state.get(QStringLiteral("k")).has_value());
        QVERIFY(state.set(QStringLiteral("k"), QStringLiteral("v1")));
        QCOMPARE(state.get(QStringLiteral("k")).value(), QStringLiteral("v1"));
        QVERIFY(state.set(QStringLiteral("k"), QStringLiteral("v2"))); // upsert
        QCOMPARE(state.get(QStringLiteral("k")).value(), QStringLiteral("v2"));
        QVERIFY(state.remove(QStringLiteral("k")));
        QVERIFY(!state.get(QStringLiteral("k")).has_value());
    }
};

QTEST_GUILESS_MAIN(EventsTest)
#include "tst_events.moc"

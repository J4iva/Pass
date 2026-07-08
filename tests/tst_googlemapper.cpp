// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/GoogleEventMapper.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QtTest>

using namespace pass;

// JSON de prueba a nivel de fichero. moc no digiere los literales en crudo
// R"(...)", así que se usan literales normales con comillas escapadas.
namespace {

const char* kTimedJson =
    "{ \"id\": \"abc123\", \"etag\": \"\\\"etag-1\\\"\", \"status\": \"confirmed\","
    "  \"summary\": \"Clase de algebra\", \"description\": \"Aula 12\","
    "  \"start\": { \"dateTime\": \"2026-06-10T09:00:00+02:00\" },"
    "  \"end\":   { \"dateTime\": \"2026-06-10T10:30:00+02:00\" },"
    "  \"updated\": \"2026-06-01T12:00:00.000Z\" }";

const char* kAllDayJson =
    "{ \"id\": \"day1\", \"status\": \"confirmed\", \"summary\": \"Festivo\","
    "  \"start\": { \"date\": \"2026-06-10\" }, \"end\": { \"date\": \"2026-06-11\" } }";

const char* kCancelledJson = "{ \"id\": \"gone\", \"status\": \"cancelled\" }";

QJsonObject parse(const char* json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

} // namespace

class GoogleMapperTest : public QObject {
    Q_OBJECT

private slots:
    void timedEventFromJson() {
        const CalendarEvent e = GoogleEventMapper::fromJson(parse(kTimedJson));
        QCOMPARE(e.provider, QStringLiteral("google"));
        QCOMPARE(e.externalId, QStringLiteral("abc123"));
        QCOMPARE(e.etag, QStringLiteral("\"etag-1\""));
        QCOMPARE(e.title, QStringLiteral("Clase de algebra"));
        QCOMPARE(e.description, QStringLiteral("Aula 12"));
        QVERIFY(!e.allDay);
        // 09:00 +02:00 == 07:00 UTC
        QCOMPARE(e.startUtc, QDateTime(QDate(2026, 6, 10), QTime(7, 0), QTimeZone::utc()));
        QCOMPARE(e.endUtc, QDateTime(QDate(2026, 6, 10), QTime(8, 30), QTimeZone::utc()));
    }

    void allDayEventFromJson() {
        const CalendarEvent e = GoogleEventMapper::fromJson(parse(kAllDayJson));
        QVERIFY(e.allDay);
        QCOMPARE(e.startUtc, QDateTime(QDate(2026, 6, 10), QTime(0, 0)).toUTC());
        QCOMPARE(e.endUtc, QDateTime(QDate(2026, 6, 11), QTime(0, 0)).toUTC());
    }

    void cancelledFlag() {
        bool cancelled = false;
        const CalendarEvent e = GoogleEventMapper::fromJson(parse(kCancelledJson), &cancelled);
        QVERIFY(cancelled);
        QCOMPARE(e.externalId, QStringLiteral("gone"));
    }

    void notCancelledFlag() {
        bool cancelled = true;
        GoogleEventMapper::fromJson(parse(kTimedJson), &cancelled);
        QVERIFY(!cancelled);
    }

    void timedEventToJson() {
        CalendarEvent e;
        e.title = QStringLiteral("Reunion");
        e.description = QStringLiteral("Online");
        e.allDay = false;
        e.startUtc = QDateTime(QDate(2026, 6, 10), QTime(7, 0), QTimeZone::utc());
        e.endUtc = QDateTime(QDate(2026, 6, 10), QTime(8, 0), QTimeZone::utc());

        const QJsonObject obj = GoogleEventMapper::toJson(e);
        QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("Reunion"));
        const QString startDt = obj.value(QStringLiteral("start"))
                                    .toObject()
                                    .value(QStringLiteral("dateTime"))
                                    .toString();
        QCOMPARE(QDateTime::fromString(startDt, Qt::ISODate).toUTC(), e.startUtc);
    }

    void allDayToJsonUsesDate() {
        CalendarEvent e;
        e.title = QStringLiteral("Vacaciones");
        e.allDay = true;
        e.startUtc = QDateTime(QDate(2026, 7, 1), QTime(0, 0)).toUTC();
        e.endUtc = QDateTime(QDate(2026, 7, 8), QTime(0, 0)).toUTC();

        const QJsonObject obj = GoogleEventMapper::toJson(e);
        const QJsonObject start = obj.value(QStringLiteral("start")).toObject();
        const QJsonObject end = obj.value(QStringLiteral("end")).toObject();
        QVERIFY(start.contains(QStringLiteral("date")));
        QVERIFY(!start.contains(QStringLiteral("dateTime")));
        QCOMPARE(start.value(QStringLiteral("date")).toString(), QStringLiteral("2026-07-01"));
        QCOMPARE(end.value(QStringLiteral("date")).toString(), QStringLiteral("2026-07-08"));
    }

    void roundTripTimed() {
        CalendarEvent e;
        e.title = QStringLiteral("Tutoria");
        e.allDay = false;
        e.startUtc = QDateTime(QDate(2026, 6, 10), QTime(15, 0), QTimeZone::utc());
        e.endUtc = QDateTime(QDate(2026, 6, 10), QTime(16, 0), QTimeZone::utc());

        QJsonObject obj = GoogleEventMapper::toJson(e);
        obj.insert(QStringLiteral("id"), QStringLiteral("rt1"));
        const CalendarEvent back = GoogleEventMapper::fromJson(obj);
        QCOMPARE(back.title, e.title);
        QCOMPARE(back.startUtc, e.startUtc);
        QCOMPARE(back.endUtc, e.endUtc);
        QCOMPARE(back.allDay, e.allDay);
    }
};

QTEST_GUILESS_MAIN(GoogleMapperTest)
#include "tst_googlemapper.moc"

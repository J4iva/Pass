// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/sync/SyncSerializer.h"

#include <QtTest>

using namespace pass;
using namespace pass::sync;

namespace {

QDateTime utc(const QString& iso) {
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

QString iso(const QDateTime& dt) {
    return dt.toUTC().toString(Qt::ISODate);
}

} // namespace

class SyncSerializerTest : public QObject {
    Q_OBJECT

private slots:
    void subjectRoundTrip() {
        Subject s;
        s.id = QUuid::createUuid();
        s.name = QStringLiteral("Álgebra");
        s.colorHex = QStringLiteral("#3366ff");
        s.archived = true;
        s.updatedAt = utc(QStringLiteral("2026-03-01T12:00:00Z"));

        Subject out;
        QVERIFY(fromJson(toJson(s), out));
        QCOMPARE(out.id, s.id);
        QCOMPARE(out.name, s.name);
        QCOMPARE(out.colorHex, s.colorHex);
        QCOMPARE(out.archived, s.archived);
        QCOMPARE(iso(out.updatedAt), iso(s.updatedAt));
    }

    void topicRoundTrip() {
        Topic t;
        t.id = QUuid::createUuid();
        t.subjectId = QUuid::createUuid();
        t.name = QStringLiteral("Integrales");
        t.updatedAt = utc(QStringLiteral("2026-03-01T12:00:00Z"));

        Topic out;
        QVERIFY(fromJson(toJson(t), out));
        QCOMPARE(out.id, t.id);
        QCOMPARE(out.subjectId, t.subjectId);
        QCOMPARE(out.name, t.name);
        QCOMPARE(iso(out.updatedAt), iso(t.updatedAt));

        // subject_id obligatorio: sin él, se rechaza.
        QJsonObject noSubject = toJson(t);
        noSubject.remove(QStringLiteral("subject_id"));
        Topic bad;
        QVERIFY(!fromJson(noSubject, bad));
    }

    void strategyRoundTrip() {
        PomodoroStrategy s;
        s.id = QUuid::createUuid();
        s.name = QStringLiteral("Maratón");
        s.workMinutes = 90;
        s.breakMinutes = 15;
        s.longBreakMinutes = 30;
        s.cyclesBeforeLongBreak = 2;
        s.updatedAt = utc(QStringLiteral("2026-03-01T12:00:00Z"));

        PomodoroStrategy out;
        QVERIFY(fromJson(toJson(s), out));
        QCOMPARE(out.id, s.id);
        QCOMPARE(out.name, s.name);
        QCOMPARE(out.workMinutes, 90);
        QCOMPARE(out.cyclesBeforeLongBreak, 2);
        QVERIFY(!out.builtin); // el espejo solo contiene personalizadas
    }

    void strategyClampsOutOfRangeIntegers() {
        // Frontera de sync (H-1/B-1): enteros fuera de rango se acotan al entrar,
        // no se aceptan crudos. work [1,600], break/long [0,600], cycles [0,100].
        QJsonObject o{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"), QStringLiteral("Maliciosa")},
            {QStringLiteral("work_min"), 0},              // -> 1
            {QStringLiteral("break_min"), -100000},       // -> 0
            {QStringLiteral("long_break_min"), 999999},   // -> 600
            {QStringLiteral("cycles_before_long"), -5},   // -> 0
            {QStringLiteral("updated_at"), QStringLiteral("2026-01-01T00:00:00Z")}};
        PomodoroStrategy out;
        QVERIFY(fromJson(o, out));
        QCOMPARE(out.workMinutes, 1);
        QCOMPARE(out.breakMinutes, 0);
        QCOMPARE(out.longBreakMinutes, 600);
        QCOMPARE(out.cyclesBeforeLongBreak, 0);
    }

    void eventRoundTrip() {
        CalendarEvent e;
        e.id = QUuid::createUuid();
        e.title = QStringLiteral("[T] Entrega");
        e.description = QStringLiteral("desc");
        e.startUtc = utc(QStringLiteral("2026-04-01T09:00:00Z"));
        e.endUtc = utc(QStringLiteral("2026-04-01T10:00:00Z"));
        e.allDay = false;
        e.subjectId = QUuid::createUuid();
        e.updatedAt = utc(QStringLiteral("2026-03-01T12:00:00Z"));

        CalendarEvent out;
        QVERIFY(fromJson(toJson(e), out));
        QCOMPARE(out.id, e.id);
        QCOMPARE(out.title, e.title);
        QCOMPARE(out.subjectId, e.subjectId);
        QCOMPARE(out.provider, QStringLiteral("local"));
        QCOMPARE(iso(out.startUtc), iso(e.startUtc));
    }

    void sessionRoundTripWithLocalEvent() {
        StudySession s;
        s.id = QUuid::createUuid();
        s.subjectId = QUuid::createUuid();
        s.topic = QStringLiteral("tema");
        s.actualSeconds = 1500;
        s.status = SessionStatus::Completed;
        s.startedAt = utc(QStringLiteral("2026-04-01T09:00:00Z"));
        s.endedAt = utc(QStringLiteral("2026-04-01T09:25:00Z"));
        s.updatedAt = utc(QStringLiteral("2026-04-01T09:25:00Z"));

        SessionEventRef ref;
        ref.kind = SessionEventRef::Kind::Local;
        ref.localId = QUuid::createUuid();

        StudySession out;
        SessionEventRef outRef;
        QVERIFY(fromJson(toJson(s, ref), out, outRef));
        QCOMPARE(out.id, s.id);
        QCOMPARE(out.subjectId, s.subjectId);
        QCOMPARE(out.actualSeconds, 1500);
        QCOMPARE(out.status, SessionStatus::Completed);
        QCOMPARE(outRef.kind, SessionEventRef::Kind::Local);
        QCOMPARE(outRef.localId, ref.localId);
    }

    void sessionRoundTripWithRemoteEvent() {
        StudySession s;
        s.id = QUuid::createUuid();
        s.updatedAt = utc(QStringLiteral("2026-04-01T09:25:00Z"));
        SessionEventRef ref;
        ref.kind = SessionEventRef::Kind::Remote;
        ref.provider = QStringLiteral("google");
        ref.externalId = QStringLiteral("EXT-123");

        StudySession out;
        SessionEventRef outRef;
        QVERIFY(fromJson(toJson(s, ref), out, outRef));
        QCOMPARE(outRef.kind, SessionEventRef::Kind::Remote);
        QCOMPARE(outRef.provider, QStringLiteral("google"));
        QCOMPARE(outRef.externalId, QStringLiteral("EXT-123"));
    }

    void sessionNoEvent() {
        StudySession s;
        s.id = QUuid::createUuid();
        s.updatedAt = utc(QStringLiteral("2026-04-01T09:25:00Z"));
        StudySession out;
        SessionEventRef outRef;
        QVERIFY(fromJson(toJson(s, {}), out, outRef));
        QCOMPARE(outRef.kind, SessionEventRef::Kind::None);
    }

    void sessionResumeProgressIsOptional() {
        StudySession s;
        s.id = QUuid::createUuid();
        s.status = SessionStatus::Aborted;
        s.updatedAt = utc(QStringLiteral("2026-04-01T09:25:00Z"));
        s.resumePhaseIndex = 2;
        s.resumeElapsedSec = 75;

        // Retomable: los campos viajan en el JSON.
        const QJsonObject json = toJson(s, {});
        QVERIFY(json.contains(QStringLiteral("resume_phase_index")));
        StudySession out;
        SessionEventRef outRef;
        QVERIFY(fromJson(json, out, outRef));
        QCOMPARE(out.resumePhaseIndex, 2);
        QCOMPARE(out.resumeElapsedSec, 75);

        // No retomable: campos ausentes => -1/0 al leer (retrocompatible).
        StudySession plain;
        plain.id = QUuid::createUuid();
        plain.updatedAt = s.updatedAt;
        const QJsonObject plainJson = toJson(plain, {});
        QVERIFY(!plainJson.contains(QStringLiteral("resume_phase_index")));
        StudySession out2;
        SessionEventRef outRef2;
        QVERIFY(fromJson(plainJson, out2, outRef2));
        QCOMPARE(out2.resumePhaseIndex, -1);
        QCOMPARE(out2.resumeElapsedSec, 0);
    }

    void tombstoneRoundTrip() {
        TombstoneRecord t;
        t.entity = QStringLiteral("subjects");
        t.id = QUuid::createUuid();
        t.deletedAt = utc(QStringLiteral("2026-05-01T00:00:00Z"));
        TombstoneRecord out;
        QVERIFY(fromJson(toJson(t), out));
        QCOMPARE(out.entity, t.entity);
        QCOMPARE(out.id, t.id);
        QCOMPARE(iso(out.deletedAt), iso(t.deletedAt));
    }

    void rejectsBadInput() {
        // id ausente / no-UUID.
        QJsonObject noId{{QStringLiteral("name"), QStringLiteral("X")},
                         {QStringLiteral("updated_at"), QStringLiteral("2026-01-01T00:00:00Z")}};
        Subject s;
        QVERIFY(!fromJson(noId, s));

        QJsonObject badId{{QStringLiteral("id"), QStringLiteral("no-soy-un-uuid")},
                          {QStringLiteral("name"), QStringLiteral("X")},
                          {QStringLiteral("updated_at"), QStringLiteral("2026-01-01T00:00:00Z")}};
        QVERIFY(!fromJson(badId, s));

        // updated_at ausente.
        QJsonObject noDate{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                           {QStringLiteral("name"), QStringLiteral("X")}};
        QVERIFY(!fromJson(noDate, s));

        // Tipo equivocado en un campo numérico de estrategia.
        QJsonObject badStrategy{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"), QStringLiteral("X")},
            {QStringLiteral("work_min"), QStringLiteral("no-numero")},
            {QStringLiteral("break_min"), 5},
            {QStringLiteral("long_break_min"), 15},
            {QStringLiteral("cycles_before_long"), 4},
            {QStringLiteral("updated_at"), QStringLiteral("2026-01-01T00:00:00Z")}};
        PomodoroStrategy st;
        QVERIFY(!fromJson(badStrategy, st));
    }

    void ignoresUnknownFields() {
        Subject s;
        s.id = QUuid::createUuid();
        s.name = QStringLiteral("X");
        s.updatedAt = utc(QStringLiteral("2026-01-01T00:00:00Z"));
        QJsonObject o = toJson(s);
        o.insert(QStringLiteral("campo_desconocido"), QStringLiteral("ignórame"));
        Subject out;
        QVERIFY(fromJson(o, out));
        QCOMPARE(out.id, s.id);
    }
};

QTEST_GUILESS_MAIN(SyncSerializerTest)
#include "tst_syncserializer.moc"

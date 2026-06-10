// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/db/Database.h"
#include "pass/repo/SessionRepository.h"
#include "pass/repo/SubjectRepository.h"
#include "pass/stats/StatsService.h"

#include <QtTest>

using namespace pass;

class StatsTest : public QObject {
    Q_OBJECT

private:
    StudySession makeSession(const QUuid& subjectId, int workSeconds, SessionStatus status,
                             const QDateTime& startedUtc) {
        StudySession s;
        s.id = QUuid::createUuid();
        s.subjectId = subjectId;
        s.actualSeconds = workSeconds;
        s.startedAt = startedUtc;
        s.endedAt = startedUtc.addSecs(workSeconds);
        s.status = status;
        return s;
    }

private slots:
    void aggregatesHoursBySubject() {
        Database db(QStringLiteral(":memory:"));
        SubjectRepository subjects(db.handle());
        SessionRepository sessions(db.handle());
        StatsService stats(db.handle());

        Subject algebra{QUuid::createUuid(), QStringLiteral("Álgebra"), QStringLiteral("#111111"),
                        false};
        Subject fisica{QUuid::createUuid(), QStringLiteral("Física"), QStringLiteral("#222222"),
                       false};
        QVERIFY(subjects.add(algebra));
        QVERIFY(subjects.add(fisica));

        const auto now = QDateTime::currentDateTimeUtc();
        QVERIFY(sessions.add(makeSession(algebra.id, 3600, SessionStatus::Completed, now)));
        QVERIFY(sessions.add(makeSession(algebra.id, 1800, SessionStatus::Aborted, now)));
        QVERIFY(sessions.add(makeSession(fisica.id, 600, SessionStatus::Completed, now)));
        // Las planificadas y las de 0 segundos no cuentan.
        QVERIFY(sessions.add(makeSession(fisica.id, 0, SessionStatus::Planned, now)));

        const auto rows = stats.hoursBySubject();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].subjectName, QStringLiteral("Álgebra"));
        QCOMPARE(rows[0].workSeconds, 5400);
        QCOMPARE(rows[1].subjectName, QStringLiteral("Física"));
        QCOMPARE(rows[1].workSeconds, 600);

        QCOMPARE(stats.completedSessionCount(), 2);
        QCOMPARE(stats.totalWorkSeconds(), 6000);
    }

    void buildsContinuousDailySeries() {
        Database db(QStringLiteral(":memory:"));
        SessionRepository sessions(db.handle());
        StatsService stats(db.handle());

        const auto now = QDateTime::currentDateTimeUtc();
        QVERIFY(sessions.add(makeSession({}, 1200, SessionStatus::Completed, now)));

        const auto series = stats.minutesPerDay(7);
        QCOMPARE(series.size(), 7);
        QCOMPARE(series.last().date, QDate::currentDate());
        QCOMPARE(series.last().minutes, 20);
        for (int i = 0; i < 6; ++i)
            QCOMPARE(series[i].minutes, 0);
    }
};

QTEST_GUILESS_MAIN(StatsTest)
#include "tst_stats.moc"

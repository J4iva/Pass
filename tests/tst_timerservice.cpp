// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/session/SessionTimerService.h"

#include <QSignalSpy>
#include <QtTest>

using namespace pass;
using Phase = SessionTimerService::Phase;
using State = SessionTimerService::State;

class TimerServiceTest : public QObject {
    Q_OBJECT

private slots:
    void runsThroughPhasesAndFinishes() {
        SessionTimerService timer;
        QSignalSpy phaseSpy(&timer, &SessionTimerService::phaseChanged);
        QSignalSpy finishedSpy(&timer, &SessionTimerService::finished);

        timer.startWithPhases({{Phase::Work, 1}, {Phase::ShortBreak, 1}, {Phase::Work, 1}},
                              QUuid::createUuid(), QStringLiteral("integrales"));
        QCOMPARE(timer.state(), State::Running);
        QCOMPARE(phaseSpy.count(), 1); // fase inicial Work

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
        QCOMPARE(timer.state(), State::Finished);
        QCOMPARE(phaseSpy.count(), 3); // Work, ShortBreak, Work

        const auto session = finishedSpy.takeFirst().at(0).value<StudySession>();
        QCOMPARE(session.status, SessionStatus::Completed);
        QCOMPARE(session.actualSeconds, 2); // solo fases de trabajo
        QVERIFY(session.startedAt.isValid());
        QVERIFY(session.endedAt.isValid());
        QCOMPARE(session.topic, QStringLiteral("integrales"));
    }

    void pauseFreezesElapsedTime() {
        SessionTimerService timer;
        timer.startWithPhases({{Phase::Work, 60}}, {}, {});

        timer.pause();
        QCOMPARE(timer.state(), State::Paused);
        const int remainingAtPause = timer.remainingSeconds();
        QTest::qWait(1500);
        QCOMPARE(timer.remainingSeconds(), remainingAtPause);

        timer.resume();
        QCOMPARE(timer.state(), State::Running);
    }

    void abortReportsAccumulatedWork() {
        SessionTimerService timer;
        QSignalSpy finishedSpy(&timer, &SessionTimerService::finished);
        timer.startWithPhases({{Phase::Work, 60}}, {}, {});

        QTest::qWait(1200);
        timer.abort();
        QCOMPARE(timer.state(), State::Aborted);
        QCOMPARE(finishedSpy.count(), 1);

        const auto session = finishedSpy.takeFirst().at(0).value<StudySession>();
        QCOMPARE(session.status, SessionStatus::Aborted);
        QVERIFY(session.actualSeconds >= 1);
    }

    void abortIsNotResumable() {
        SessionTimerService timer;
        QSignalSpy finishedSpy(&timer, &SessionTimerService::finished);
        timer.startWithPhases({{Phase::Work, 60}}, {}, {});
        timer.abort();
        const auto session = finishedSpy.takeFirst().at(0).value<StudySession>();
        QCOMPARE(session.resumePhaseIndex, -1); // terminar a mano no deja retomar
    }

    void interruptCapturesPosition() {
        SessionTimerService timer;
        QSignalSpy finishedSpy(&timer, &SessionTimerService::finished);
        // Tras 1s la 1ª fase (Work) acaba y entra en el descanso (índice 1).
        timer.startWithPhases({{Phase::Work, 1}, {Phase::ShortBreak, 60}, {Phase::Work, 60}}, {},
                              {});
        QTRY_VERIFY_WITH_TIMEOUT(timer.currentPhaseIndex() >= 1, 5000);
        timer.interrupt();
        QCOMPARE(timer.state(), State::Aborted);

        const auto session = finishedSpy.takeFirst().at(0).value<StudySession>();
        QCOMPARE(session.status, SessionStatus::Aborted);
        QCOMPARE(session.resumePhaseIndex, 1);     // cortada en el descanso
        QVERIFY(session.resumeElapsedSec >= 0);
        QCOMPARE(session.actualSeconds, 1);        // solo el bloque Work ya completado
    }

    void resumeStartsAtSavedPosition() {
        SessionTimerService timer;
        // Reanudar en la 3ª fase (índice 2, Work) con 40 s ya consumidos.
        timer.startWithPhases({{Phase::Work, 100}, {Phase::ShortBreak, 50}, {Phase::Work, 100}}, {},
                              {}, {}, 0, /*resumePhaseIndex=*/2, /*resumePhaseElapsedSec=*/40);
        QCOMPARE(timer.currentPhaseIndex(), 2);
        QCOMPARE(timer.phase(), Phase::Work);
        QVERIFY(qAbs(timer.remainingSeconds() - 60) <= 1);    // 100 - 40
        QVERIFY(qAbs(timer.elapsedWorkSeconds() - 140) <= 1); // Work previa (100) + 40
    }

    void cannotStartTwice() {
        SessionTimerService timer;
        timer.startWithPhases({{Phase::Work, 60}}, {}, {});
        QSignalSpy stateSpy(&timer, &SessionTimerService::stateChanged);
        timer.startWithPhases({{Phase::Work, 60}}, {}, {});
        QCOMPARE(stateSpy.count(), 0); // la segunda llamada se ignora
    }
};

QTEST_GUILESS_MAIN(TimerServiceTest)
#include "tst_timerservice.moc"

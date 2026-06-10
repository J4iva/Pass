// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/StudySession.h"
#include "pass/session/StrategyCatalog.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>

namespace pass {

// Temporizador de sesiones de estudio con fases trabajo/descanso.
//
// El tiempo real se mide con QElapsedTimer acumulado entre pausas; el QTimer
// interno solo refresca la UI (1 s), así el conteo sobrevive a ticks perdidos
// o a la suspensión del equipo.
class SessionTimerService : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Running, Paused, Finished, Aborted };
    Q_ENUM(State)
    enum class Phase { Work, ShortBreak, LongBreak };
    Q_ENUM(Phase)

    struct PhaseSpec {
        Phase phase;
        int seconds;
    };

    explicit SessionTimerService(QObject* parent = nullptr);

    void start(const SessionPlan& plan, const QUuid& subjectId, const QString& topic);
    // Seam para tests: permite fases de segundos en lugar de minutos.
    void startWithPhases(QList<PhaseSpec> phases, const QUuid& subjectId, const QString& topic,
                         const QUuid& strategyId = {}, int plannedMinutes = 0);

    void pause();
    void resume();
    void abort(); // termina la sesión registrando el trabajo acumulado

    State state() const { return m_state; }
    Phase phase() const;
    int remainingSeconds() const;
    int elapsedWorkSeconds() const;

signals:
    void tick(int remainingSeconds, pass::SessionTimerService::Phase phase);
    void phaseChanged(pass::SessionTimerService::Phase phase);
    void stateChanged(pass::SessionTimerService::State state);
    // Emitida al completar o abortar; el llamante decide persistirla.
    void finished(const pass::StudySession& session);

private:
    void setState(State state);
    void onTick();
    void advancePhase(qint64 overshootMs);
    qint64 phaseElapsedMs() const;
    void finish(SessionStatus status);

    QTimer m_uiTimer;
    QElapsedTimer m_elapsed;   // corre solo en estado Running
    qint64 m_accumPhaseMs = 0; // acumulado de la fase actual entre pausas
    qint64 m_workMs = 0;       // trabajo total acumulado (fases Work ya cerradas)
    QList<PhaseSpec> m_phases;
    int m_phaseIndex = 0;
    State m_state = State::Idle;
    StudySession m_session;
};

} // namespace pass

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/session/SessionTimerService.h"

namespace pass {

SessionTimerService::SessionTimerService(QObject* parent) : QObject(parent) {
    m_uiTimer.setInterval(1000);
    connect(&m_uiTimer, &QTimer::timeout, this, &SessionTimerService::onTick);
}

QList<SessionTimerService::PhaseSpec> SessionTimerService::phasesFor(const SessionPlan& plan) {
    QList<PhaseSpec> phases;
    for (int cycle = 1; cycle <= plan.cycles; ++cycle) {
        phases.append({Phase::Work, plan.strategy.workMinutes * 60});
        if (cycle == plan.cycles)
            break; // sin descanso tras el último bloque
        const bool longBreak = plan.strategy.cyclesBeforeLongBreak > 0 &&
                               cycle % plan.strategy.cyclesBeforeLongBreak == 0;
        phases.append({longBreak ? Phase::LongBreak : Phase::ShortBreak,
                       (longBreak ? plan.strategy.longBreakMinutes : plan.strategy.breakMinutes) *
                           60});
    }
    return phases;
}

void SessionTimerService::start(const SessionPlan& plan, const QUuid& subjectId,
                                const QString& topic, int resumePhaseIndex,
                                int resumePhaseElapsedSec) {
    startWithPhases(phasesFor(plan), subjectId, topic, plan.strategy.id, plan.totalMinutes,
                    resumePhaseIndex, resumePhaseElapsedSec);
}

void SessionTimerService::startWithPhases(QList<PhaseSpec> phases, const QUuid& subjectId,
                                          const QString& topic, const QUuid& strategyId,
                                          int plannedMinutes, int resumePhaseIndex,
                                          int resumePhaseElapsedSec) {
    if (m_state == State::Running || m_state == State::Paused)
        return;
    if (phases.isEmpty())
        return;

    m_phases = std::move(phases);
    // Reanudación: situarse en la fase guardada y recomponer el trabajo ya hecho
    // (suma de las fases Work anteriores). Por defecto (0, 0) empieza de cero.
    m_phaseIndex = qBound(0, resumePhaseIndex, int(m_phases.size()) - 1);
    m_accumPhaseMs = qint64(qMax(0, resumePhaseElapsedSec)) * 1000;
    m_workMs = 0;
    for (int i = 0; i < m_phaseIndex; ++i)
        if (m_phases[i].phase == Phase::Work)
            m_workMs += qint64(m_phases[i].seconds) * 1000;

    m_session = StudySession{};
    m_session.id = QUuid::createUuid();
    m_session.subjectId = subjectId;
    m_session.strategyId = strategyId;
    m_session.topic = topic;
    m_session.plannedMinutes = plannedMinutes;
    m_session.startedAt = QDateTime::currentDateTimeUtc();

    m_elapsed.start();
    m_uiTimer.start();
    setState(State::Running);
    emit phaseChanged(m_phases[m_phaseIndex].phase);
    emit tick(remainingSeconds(), m_phases[m_phaseIndex].phase);
}

void SessionTimerService::pause() {
    if (m_state != State::Running)
        return;
    m_accumPhaseMs += m_elapsed.elapsed();
    m_elapsed.invalidate();
    m_uiTimer.stop();
    setState(State::Paused);
}

void SessionTimerService::resume() {
    if (m_state != State::Paused)
        return;
    m_elapsed.start();
    m_uiTimer.start();
    setState(State::Running);
    emit tick(remainingSeconds(), phase());
}

void SessionTimerService::abort() {
    if (m_state != State::Running && m_state != State::Paused)
        return;
    if (m_phases[m_phaseIndex].phase == Phase::Work)
        m_workMs += phaseElapsedMs();
    finish(SessionStatus::Aborted);
}

void SessionTimerService::interrupt() {
    if (m_state != State::Running && m_state != State::Paused)
        return;
    const int idx = m_phaseIndex;
    const qint64 elapsedMs = phaseElapsedMs();
    const qint64 totalMs = qint64(m_phases[idx].seconds) * 1000;
    const bool lastPhase = idx == m_phases.size() - 1;
    if (lastPhase && elapsedMs >= totalMs) {
        // No queda tiempo: equivale a completar la sesión (no retomable).
        if (m_phases[idx].phase == Phase::Work)
            m_workMs += totalMs;
        finish(SessionStatus::Completed);
        return;
    }
    // Retomable: registrar el trabajo hecho y guardar la posición exacta.
    if (m_phases[idx].phase == Phase::Work)
        m_workMs += elapsedMs;
    m_session.resumePhaseIndex = idx;
    m_session.resumeElapsedSec = int(elapsedMs / 1000);
    finish(SessionStatus::Aborted);
}

SessionTimerService::Phase SessionTimerService::phase() const {
    if (m_phaseIndex >= 0 && m_phaseIndex < m_phases.size())
        return m_phases[m_phaseIndex].phase;
    return Phase::Work;
}

int SessionTimerService::remainingSeconds() const {
    if (m_phaseIndex < 0 || m_phaseIndex >= m_phases.size())
        return 0;
    const qint64 totalMs = qint64(m_phases[m_phaseIndex].seconds) * 1000;
    return int(qMax<qint64>(0, totalMs - phaseElapsedMs()) / 1000);
}

int SessionTimerService::elapsedWorkSeconds() const {
    qint64 ms = m_workMs;
    if ((m_state == State::Running || m_state == State::Paused) &&
        m_phases[m_phaseIndex].phase == Phase::Work)
        ms += phaseElapsedMs();
    return int(ms / 1000);
}

void SessionTimerService::setState(State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void SessionTimerService::onTick() {
    if (m_state != State::Running)
        return;
    const qint64 totalMs = qint64(m_phases[m_phaseIndex].seconds) * 1000;
    const qint64 elapsedMs = phaseElapsedMs();
    if (elapsedMs >= totalMs) {
        advancePhase(elapsedMs - totalMs);
        return;
    }
    emit tick(int((totalMs - elapsedMs) / 1000), m_phases[m_phaseIndex].phase);
}

void SessionTimerService::advancePhase(qint64 overshootMs) {
    if (m_phases[m_phaseIndex].phase == Phase::Work)
        m_workMs += qint64(m_phases[m_phaseIndex].seconds) * 1000;

    ++m_phaseIndex;
    if (m_phaseIndex >= m_phases.size()) {
        finish(SessionStatus::Completed);
        return;
    }

    // El exceso del tick (p. ej. tras volver de suspensión) se traslada a la
    // fase siguiente para no perder tiempo real.
    m_accumPhaseMs = overshootMs;
    m_elapsed.start();
    emit phaseChanged(m_phases[m_phaseIndex].phase);
    emit tick(remainingSeconds(), m_phases[m_phaseIndex].phase);
}

qint64 SessionTimerService::phaseElapsedMs() const {
    return m_accumPhaseMs + (m_elapsed.isValid() ? m_elapsed.elapsed() : 0);
}

void SessionTimerService::finish(SessionStatus status) {
    m_uiTimer.stop();
    m_elapsed.invalidate();
    m_session.endedAt = QDateTime::currentDateTimeUtc();
    m_session.actualSeconds = int(m_workMs / 1000);
    m_session.status = status;
    setState(status == SessionStatus::Completed ? State::Finished : State::Aborted);
    emit finished(m_session);
}

} // namespace pass

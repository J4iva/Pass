// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudyView.h"

#include "../widgets/SessionSetupDialog.h"
#include "../widgets/TimerWidget.h"

#include "pass/session/StrategyCatalog.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace pass;

StudyView::StudyView(Database& db, SessionTimerService* timer, CalendarProvider* calendar,
                     QWidget* parent)
    : QWidget(parent), m_subjects(db.handle()), m_strategies(db.handle()),
      m_sessions(db.handle()), m_timer(timer), m_calendar(calendar), m_status(new QLabel) {
    auto* timerWidget = new TimerWidget(m_timer);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color: gray;"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(timerWidget, 1);
    layout->addWidget(m_status);

    connect(timerWidget, &TimerWidget::newSessionRequested, this, &StudyView::startNewSession);
    connect(m_timer, &SessionTimerService::finished, this, &StudyView::onFinished);
}

void StudyView::startNewSession() {
    SessionSetupDialog dialog(m_subjects, m_strategies, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto plan = dialog.selectedPlan();
    if (!plan)
        return;

    if (const auto when = dialog.plannedStart()) {
        planSession(*plan, *when, dialog.resolveSubjectId(), dialog.topic());
        return;
    }

    m_activePlanned.reset();
    m_timer->start(*plan, dialog.resolveSubjectId(), dialog.topic());
    m_status->clear();
}

void StudyView::planSession(const SessionPlan& plan, const QDateTime& startLocal,
                            const QUuid& subjectId, const QString& topic) {
    StudySession session;
    session.id = QUuid::createUuid();
    session.subjectId = subjectId;
    session.strategyId = plan.strategy.id;
    session.topic = topic;
    session.plannedMinutes = plan.totalMinutes;
    session.startedAt = startLocal.toUTC();
    session.status = SessionStatus::Planned;

    QString subjectName;
    if (const auto subject = m_subjects.byId(subjectId))
        subjectName = subject->name;
    CalendarEvent event;
    event.title = subjectName.isEmpty()
                      ? (topic.isEmpty() ? tr("Sesión de estudio") : tr("Estudio: %1").arg(topic))
                      : tr("Estudio: %1").arg(subjectName);
    event.description = topic;
    event.startUtc = startLocal.toUTC();
    event.endUtc = startLocal.addSecs(qint64(plan.totalMinutes) * 60).toUTC();
    event.subjectId = subjectId;
    event.sourceSessionId = session.id;

    if (!m_calendar->addEvent(event)) {
        m_status->setText(tr("⚠ No se pudo crear el evento en el calendario"));
        return;
    }
    session.linkedEventId = event.id;
    if (m_sessions.add(session))
        m_status->setText(tr("Sesión planificada para el %1")
                              .arg(startLocal.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
    else
        m_status->setText(tr("⚠ No se pudo guardar la sesión planificada"));
}

void StudyView::startPlannedSession(const StudySession& planned) {
    if (m_timer->state() == SessionTimerService::State::Running ||
        m_timer->state() == SessionTimerService::State::Paused) {
        m_status->setText(tr("Ya hay una sesión en marcha"));
        return;
    }

    // Reconstruye el plan a partir de la estrategia y los minutos planificados.
    std::optional<SessionPlan> plan;
    if (const auto strategy = m_strategies.byId(planned.strategyId)) {
        const auto plans = StrategyCatalog::proposals(planned.plannedMinutes, {*strategy});
        if (!plans.isEmpty())
            plan = plans.first();
    }

    m_activePlanned = planned;
    if (plan) {
        m_timer->start(*plan, planned.subjectId, planned.topic);
    } else {
        // Estrategia desaparecida: bloque único de trabajo.
        m_timer->startWithPhases({{SessionTimerService::Phase::Work,
                                   qMax(1, planned.plannedMinutes) * 60}},
                                 planned.subjectId, planned.topic);
    }
    m_status->clear();
}

void StudyView::onFinished(const StudySession& session) {
    bool saved = false;
    if (m_activePlanned) {
        // La sesión venía planificada: se actualiza la fila existente.
        StudySession updated = *m_activePlanned;
        updated.startedAt = session.startedAt;
        updated.endedAt = session.endedAt;
        updated.actualSeconds = session.actualSeconds;
        updated.status = session.status;
        saved = m_sessions.update(updated);
        m_activePlanned.reset();
    } else {
        saved = m_sessions.add(session);
    }

    if (saved) {
        const int minutes = session.actualSeconds / 60;
        m_status->setText(tr("Sesión guardada: %1 min de trabajo efectivo")
                              .arg(minutes > 0 ? QString::number(minutes)
                                               : QStringLiteral("<1")));
    } else {
        m_status->setText(tr("⚠ No se pudo guardar la sesión"));
    }
}

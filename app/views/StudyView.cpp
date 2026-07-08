// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudyView.h"

#include "../util/SessionPlanner.h"
#include "../widgets/SessionSetupDialog.h"
#include "../widgets/TimerWidget.h"

#include "pass/session/StrategyCatalog.h"

#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace pass;

namespace {
// Una sesión es retomable si se interrumpió (Aborted) dejando posición guardada.
bool isResumable(const StudySession& s) {
    return s.status == SessionStatus::Aborted && s.resumePhaseIndex >= 0;
}
} // namespace

StudyView::StudyView(Database& db, SessionTimerService* timer, CalendarProvider* calendar,
                     QWidget* parent)
    : QWidget(parent), m_subjects(db.handle()), m_topics(db.handle()), m_strategies(db.handle()),
      m_sessions(db.handle()), m_events(db.handle()), m_timer(timer), m_calendar(calendar),
      m_status(new QLabel), m_plannedList(new QListWidget),
      m_startPlanned(new QPushButton(tr("▶ Empezar la seleccionada"))) {
    auto* timerWidget = new TimerWidget(m_timer);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setStyleSheet(QStringLiteral("color: gray;"));

    m_plannedList->setMaximumHeight(160);
    m_startPlanned->setEnabled(false);
    auto* plannedBox = new QGroupBox(tr("Sesiones pendientes"));
    auto* plannedLayout = new QVBoxLayout(plannedBox);
    plannedLayout->addWidget(m_plannedList);
    plannedLayout->addWidget(m_startPlanned);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(timerWidget, 1);
    layout->addWidget(m_status);
    layout->addWidget(plannedBox);

    connect(timerWidget, &TimerWidget::newSessionRequested, this, &StudyView::startNewSession);
    connect(m_timer, &SessionTimerService::finished, this, &StudyView::onFinished);
    connect(m_startPlanned, &QPushButton::clicked, this, &StudyView::startSelectedPlanned);
    connect(m_plannedList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { startSelectedPlanned(); });
    connect(m_plannedList, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool valid = row >= 0 && row < m_planned.size();
        m_startPlanned->setEnabled(valid);
        m_startPlanned->setText(valid && isResumable(m_planned[row])
                                    ? tr("⏸ Reanudar la seleccionada")
                                    : tr("▶ Empezar la seleccionada"));
    });
    // Planificar (aquí o en el calendario) crea un evento; al cambiar los eventos
    // se recarga la lista de pendientes.
    connect(m_calendar, &CalendarProvider::eventsChanged, this, &StudyView::refreshPlanned);

    refreshPlanned();
}

void StudyView::startNewSession() {
    // Tareas pendientes (próximo año) a las que se puede asignar la sesión.
    const auto now = QDateTime::currentDateTimeUtc();
    QList<CalendarEvent> tasks;
    for (const auto& e : m_events.between(now, now.addDays(365))) {
        if (isTask(e))
            tasks.append(e);
    }

    SessionSetupDialog dialog(m_subjects, m_topics, m_strategies, tasks, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto plan = dialog.selectedPlan();
    if (!plan)
        return;

    if (const auto when = dialog.plannedStart()) {
        planSession(*plan, *when, dialog.resolveSubjectId(), dialog.topic(),
                    dialog.selectedTaskId());
        return;
    }

    m_activePlanned.reset();
    m_pendingTaskId = dialog.selectedTaskId();
    m_timer->start(*plan, dialog.resolveSubjectId(), dialog.topic());
    m_status->clear();
}

void StudyView::planSession(const SessionPlan& plan, const QDateTime& startLocal,
                            const QUuid& subjectId, const QString& topic, const QUuid& taskId) {
    switch (util::planSession(m_subjects, m_sessions, *m_calendar, plan, startLocal, subjectId,
                              topic, taskId)) {
    case util::PlanStatus::Ok:
        m_status->setText(tr("Sesión planificada para el %1")
                              .arg(startLocal.toString(QStringLiteral("dd/MM/yyyy HH:mm"))));
        break;
    case util::PlanStatus::CalendarFailed:
        m_status->setText(tr("⚠ No se pudo crear el evento en el calendario"));
        break;
    case util::PlanStatus::SaveFailed:
        m_status->setText(tr("⚠ No se pudo guardar la sesión planificada"));
        break;
    }
}

QList<SessionTimerService::PhaseSpec> StudyView::phasesForSession(const StudySession& s) const {
    if (const auto strategy = m_strategies.byId(s.strategyId)) {
        const auto plans = StrategyCatalog::proposals(s.plannedMinutes, {*strategy});
        if (!plans.isEmpty())
            return SessionTimerService::phasesFor(plans.first());
    }
    // Estrategia desaparecida: bloque único de trabajo.
    return {{SessionTimerService::Phase::Work, qMax(1, s.plannedMinutes) * 60}};
}

int StudyView::remainingSecondsFor(const StudySession& s) const {
    const auto phases = phasesForSession(s);
    const int idx = qBound(0, s.resumePhaseIndex, int(phases.size()) - 1);
    qint64 remaining = qMax(0, phases[idx].seconds - s.resumeElapsedSec);
    for (int i = idx + 1; i < phases.size(); ++i)
        remaining += phases[i].seconds;
    return int(remaining);
}

void StudyView::startPlannedSession(const StudySession& planned) {
    if (m_timer->state() == SessionTimerService::State::Running ||
        m_timer->state() == SessionTimerService::State::Paused) {
        m_status->setText(tr("Ya hay una sesión en marcha"));
        return;
    }

    // Reanudable: arrancar en la posición guardada; planificada: desde el principio.
    const int idx = isResumable(planned) ? planned.resumePhaseIndex : 0;
    const int elapsed = isResumable(planned) ? planned.resumeElapsedSec : 0;

    m_activePlanned = planned;
    m_pendingTaskId = QUuid(); // las pendientes conservan su propio vínculo
    m_timer->startWithPhases(phasesForSession(planned), planned.subjectId, planned.topic,
                             planned.strategyId, planned.plannedMinutes, idx, elapsed);
    m_status->clear();
    refreshPlanned(); // la que arranca deja de estar "pendiente"
}

void StudyView::startSelectedPlanned() {
    const int row = m_plannedList->currentRow();
    if (row < 0 || row >= m_planned.size())
        return;
    startPlannedSession(m_planned[row]);
}

void StudyView::refreshPlanned() {
    m_planned.clear();
    for (const auto& s : m_sessions.all()) {
        // Pendientes = planificadas (sin arrancar) + interrumpidas retomables.
        if (s.status != SessionStatus::Planned && !isResumable(s))
            continue;
        // La sesión en curso (ya arrancada) no es "pendiente".
        if (m_activePlanned && s.id == m_activePlanned->id)
            continue;
        m_planned.append(s);
    }
    std::sort(m_planned.begin(), m_planned.end(),
              [](const StudySession& a, const StudySession& b) { return a.startedAt < b.startedAt; });

    m_plannedList->clear();
    for (const auto& s : m_planned) {
        QString label;
        if (isResumable(s)) {
            const int mins = (remainingSecondsFor(s) + 59) / 60; // redondeo hacia arriba
            label = tr("⏸ Reanudar — quedan %1 min").arg(mins);
        } else {
            label = s.startedAt.toLocalTime().toString(QStringLiteral("dd/MM HH:mm"));
        }
        if (const auto subject = m_subjects.byId(s.subjectId); subject && !subject->name.isEmpty())
            label += QStringLiteral("  ·  %1").arg(subject->name);
        if (!s.topic.isEmpty())
            label += QStringLiteral("  ·  %1").arg(s.topic);
        if (!isResumable(s))
            label += QStringLiteral("  (%1 min)").arg(s.plannedMinutes);
        m_plannedList->addItem(label);
    }
    if (m_planned.isEmpty())
        m_plannedList->addItem(tr("(no hay sesiones pendientes)"));
    m_startPlanned->setEnabled(false);
}

void StudyView::onFinished(const StudySession& session) {
    bool saved = false;
    if (m_activePlanned) {
        // La sesión venía de la lista de pendientes: se actualiza la fila existente.
        StudySession updated = *m_activePlanned;
        // Una planificada registra su inicio real; una retomada conserva el primero.
        if (m_activePlanned->status == SessionStatus::Planned)
            updated.startedAt = session.startedAt;
        updated.endedAt = session.endedAt;
        updated.actualSeconds = session.actualSeconds;
        updated.status = session.status;
        // Progreso de reanudación: se guarda si volvió a interrumpirse, se limpia
        // al completarla o terminarla a mano (session lo trae a -1).
        updated.resumePhaseIndex = session.resumePhaseIndex;
        updated.resumeElapsedSec = session.resumeElapsedSec;
        saved = m_sessions.update(updated);
        m_activePlanned.reset();
    } else {
        StudySession toSave = session;
        toSave.linkedEventId = m_pendingTaskId; // tarea elegida, si la hubo
        saved = m_sessions.add(toSave);
    }
    m_pendingTaskId = QUuid();

    if (saved) {
        const int minutes = session.actualSeconds / 60;
        m_status->setText(tr("Sesión guardada: %1 min de trabajo efectivo")
                              .arg(minutes > 0 ? QString::number(minutes)
                                               : QStringLiteral("<1")));
    } else {
        m_status->setText(tr("⚠ No se pudo guardar la sesión"));
    }
    refreshPlanned();
}

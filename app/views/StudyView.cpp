// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudyView.h"

#include "../widgets/SessionSetupDialog.h"
#include "../widgets/TimerWidget.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace pass;

StudyView::StudyView(Database& db, SessionTimerService* timer, QWidget* parent)
    : QWidget(parent), m_subjects(db.handle()), m_strategies(db.handle()),
      m_sessions(db.handle()), m_timer(timer), m_status(new QLabel) {
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
    m_timer->start(*plan, dialog.resolveSubjectId(), dialog.topic());
    m_status->clear();
}

void StudyView::onFinished(const StudySession& session) {
    if (m_sessions.add(session)) {
        const int minutes = session.actualSeconds / 60;
        m_status->setText(tr("Sesión guardada: %1 min de trabajo efectivo")
                              .arg(minutes > 0 ? QString::number(minutes)
                                               : QStringLiteral("<1")));
    } else {
        m_status->setText(tr("⚠ No se pudo guardar la sesión"));
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimerWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using Service = pass::SessionTimerService;

TimerWidget::TimerWidget(Service* service, QWidget* parent)
    : QWidget(parent), m_service(service), m_time(new QLabel(QStringLiteral("--:--"))),
      m_phase(new QLabel), m_newSession(new QPushButton(tr("Nueva sesión"))),
      m_pauseResume(new QPushButton(tr("Pausar"))), m_stop(new QPushButton(tr("Terminar"))) {
    QFont big = m_time->font();
    big.setPointSize(56);
    big.setBold(true);
    m_time->setFont(big);
    m_time->setAlignment(Qt::AlignCenter);

    QFont mid = m_phase->font();
    mid.setPointSize(16);
    m_phase->setFont(mid);
    m_phase->setAlignment(Qt::AlignCenter);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_newSession);
    buttons->addWidget(m_pauseResume);
    buttons->addWidget(m_stop);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_phase);
    layout->addWidget(m_time);
    layout->addLayout(buttons);
    layout->addStretch();

    connect(m_newSession, &QPushButton::clicked, this, &TimerWidget::newSessionRequested);
    connect(m_pauseResume, &QPushButton::clicked, this, [this] {
        if (m_service->state() == Service::State::Running)
            m_service->pause();
        else if (m_service->state() == Service::State::Paused)
            m_service->resume();
    });
    connect(m_stop, &QPushButton::clicked, this, [this] { m_service->abort(); });

    connect(m_service, &Service::tick, this, &TimerWidget::onTick);
    connect(m_service, &Service::stateChanged, this, &TimerWidget::onStateChanged);
    connect(m_service, &Service::phaseChanged, this, &TimerWidget::updatePhaseLabel);

    onStateChanged(m_service->state());
}

void TimerWidget::onTick(int remainingSeconds, Service::Phase phase) {
    Q_UNUSED(phase);
    m_time->setText(QStringLiteral("%1:%2")
                        .arg(remainingSeconds / 60, 2, 10, QLatin1Char('0'))
                        .arg(remainingSeconds % 60, 2, 10, QLatin1Char('0')));
}

void TimerWidget::onStateChanged(Service::State state) {
    const bool active = state == Service::State::Running || state == Service::State::Paused;
    m_newSession->setVisible(!active);
    m_pauseResume->setVisible(active);
    m_stop->setVisible(active);
    m_pauseResume->setText(state == Service::State::Paused ? tr("Reanudar") : tr("Pausar"));

    if (state == Service::State::Finished) {
        m_phase->setText(tr("¡Sesión completada!"));
        m_phase->setStyleSheet(QStringLiteral("color: #2e7d32;"));
        m_time->setText(QStringLiteral("00:00"));
    } else if (state == Service::State::Aborted) {
        m_phase->setText(tr("Sesión terminada antes de tiempo"));
        m_phase->setStyleSheet(QString());
    } else if (state == Service::State::Idle) {
        m_phase->setText(tr("Listo para estudiar"));
        m_phase->setStyleSheet(QString());
    }
}

void TimerWidget::updatePhaseLabel(Service::Phase phase) {
    switch (phase) {
    case Service::Phase::Work:
        m_phase->setText(tr("Trabajo"));
        m_phase->setStyleSheet(QStringLiteral("color: #c62828; font-weight: bold;"));
        break;
    case Service::Phase::ShortBreak:
        m_phase->setText(tr("Descanso"));
        m_phase->setStyleSheet(QStringLiteral("color: #2e7d32;"));
        break;
    case Service::Phase::LongBreak:
        m_phase->setText(tr("Descanso largo"));
        m_phase->setStyleSheet(QStringLiteral("color: #1565c0;"));
        break;
    }
}

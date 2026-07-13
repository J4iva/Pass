// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimerWidget.h"

#include "../theme/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using Service = pass::SessionTimerService;
using namespace pass::theme;

TimerWidget::TimerWidget(Service* service, QWidget* parent)
    : QWidget(parent), m_service(service), m_time(new QLabel(QStringLiteral("--:--"))),
      m_phase(new QLabel), m_newSession(new QPushButton(tr("Nueva sesión"))),
      m_pauseResume(new QPushButton(tr("Pausar"))), m_stop(new QPushButton(tr("Terminar"))) {
    // Countdown en VT323 pixel (digitos uniformes); texto de fase en mono.
    m_time->setFont(timerFont(64));
    m_time->setAlignment(Qt::AlignCenter);

    m_phase->setFont(bodyFont(13));
    m_phase->setAlignment(Qt::AlignCenter);
    m_phase->setTextFormat(Qt::RichText);

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
    updatePhaseLabel(m_service->phase());
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
        m_phase->setText(statusDotRich(kPhosphorHex, 0x25CF, tr("Sesión completada")));
        m_time->setText(QStringLiteral("00:00"));
    } else if (state == Service::State::Aborted) {
        m_phase->setText(statusDotRich(kFgFaintHex, 0x25CB, tr("Sesión terminada antes de tiempo")));
    } else if (state == Service::State::Idle) {
        m_phase->setText(statusDotRich(kFgFaintHex, 0x25CB, tr("Listo para trabajar")));
    }
}

void TimerWidget::updatePhaseLabel(Service::Phase phase) {
    switch (phase) {
    case Service::Phase::Work:
        m_phase->setText(statusDotRich(kAccentHex, 0x25CF, tr("Trabajo")));
        break;
    case Service::Phase::ShortBreak:
        m_phase->setText(statusDotRich(kFgFaintHex, 0x25CB, tr("Descanso")));
        break;
    case Service::Phase::LongBreak:
        m_phase->setText(statusDotRich(kFgHex, 0x25CF, tr("Descanso largo")));
        break;
    }
}

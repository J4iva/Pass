// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScanlineOverlay.h"

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>

ScanlineOverlay::ScanlineOverlay(QWidget* host) : QWidget(host), m_host(host) {
    // Hijo transparente del host, alzado encima: compone sobre la misma
    // superficie que el resto (aparece en capturas) y deja pasar el input.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::NoFocus);
    host->installEventFilter(this);
    syncGeometry();
    show();
    raise();
}

void ScanlineOverlay::syncGeometry() {
    if (!m_host)
        return;
    setGeometry(0, 0, m_host->width(), m_host->height());
    raise();
}

bool ScanlineOverlay::enabled() const {
    return m_enabled;
}

void ScanlineOverlay::setEnabled(bool on) {
    m_enabled = on;
    setVisible(on);
    if (on)
        raise();
}

void ScanlineOverlay::paintEvent(QPaintEvent* /*event*/) {
    // Scanlines horizontales cada 2px al ~8% de negro: suaves, estaticas.
    QPainter p(this);
    p.setPen(Qt::NoPen);
    const QColor line(0, 0, 0, 21); // ~8%
    const int w = width();
    for (int y = 0; y < height(); y += 2)
        p.fillRect(QRect(0, y, w, 1), line);
}

bool ScanlineOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_host && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        syncGeometry();
    return QWidget::eventFilter(watched, event);
}

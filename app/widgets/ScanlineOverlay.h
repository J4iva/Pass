// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

// Overlay global de scanlines CRT: ventana tool transparente, sin input (los
// clics pasan al host), hija de la ventana principal para seguirla en
// move/resize. Textura estatica (sin animacion). Toggleable (Ctrl+L).
class ScanlineOverlay : public QWidget {
    Q_OBJECT

public:
    explicit ScanlineOverlay(QWidget* host);

    void setEnabled(bool on);
    bool enabled() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void syncGeometry();

    QWidget* m_host;
    bool m_enabled = true;
};

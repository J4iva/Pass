// SPDX-License-Identifier: GPL-3.0-or-later
#include "ActivityCalendarWidget.h"

#include <QPainter>

QColor ActivityCalendarWidget::categoryColor(Category category) {
    switch (category) {
    case Study:
        return QColor(0x2F, 0x85, 0x5A); // verde muted
    case Google:
        return QColor(0x2B, 0x6C, 0xB0); // azul muted
    case Local:
        return QColor(0xB7, 0x79, 0x1F); // ámbar muted
    default:
        return QColor();
    }
}

void ActivityCalendarWidget::setActivity(const QHash<QDate, Categories>& activity) {
    m_activity = activity;
    updateCells();
}

void ActivityCalendarWidget::paintCell(QPainter* painter, const QRect& rect, QDate date) const {
    // Primero la celda normal (número, selección, día de hoy...).
    QCalendarWidget::paintCell(painter, rect, date);

    const auto it = m_activity.constFind(date);
    if (it == m_activity.constEnd() || *it == NoActivity)
        return;

    // Recoge los tipos presentes en orden fijo (estudio, google, local).
    QList<Category> present;
    for (Category c : {Study, Google, Local}) {
        if (it->testFlag(c))
            present.append(c);
    }
    if (present.isEmpty())
        return;

    // Puntos pequeños centrados horizontalmente, justo DEBAJO del número. Usamos
    // coordenadas de subpíxel (qreal + QRectF) y el centro geométrico EXACTO de la
    // celda —que es donde Qt centra el número— para que queden perfectamente
    // alineados con la cifra, sin el medio píxel de sesgo de las coordenadas enteras.
    const qreal diameter = qBound(5.0, rect.height() / 7.0, 8.0);
    const qreal gap = diameter - 2.0;
    const qreal totalWidth = present.size() * diameter + (present.size() - 1) * gap;
    const qreal cx = rect.left() + rect.width() / 2.0;  // centro horizontal real
    qreal x = cx - totalWidth / 2.0;
    qreal y = rect.top() + rect.height() / 2.0 + painter->fontMetrics().height() / 2.0 + 4.0;
    y = qMin(y, rect.bottom() - diameter - 2.0); // no salirse de la celda

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    for (Category c : present) {
        painter->setBrush(categoryColor(c));
        painter->drawEllipse(QRectF(x, y, diameter, diameter));
        x += diameter + gap;
    }
    painter->restore();
}

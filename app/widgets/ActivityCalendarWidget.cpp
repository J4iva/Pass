// SPDX-License-Identifier: GPL-3.0-or-later
#include "ActivityCalendarWidget.h"

#include "../theme/Theme.h"

#include <QPainter>

namespace {

// Mascaras de bits sobre un lattice 3x3 (indice = fila*3 + columna) que definen
// el patron de dots de cada categoria. Monocromo: la distincion es el patron.
//   Study  -> 3x3 solido (0x1FF)
//   Google -> 3x3 anillo, sin centro (0x1EF)
//   Local  -> cruz 3x3 (0xBA)
constexpr int kPatternMask[] = {0x1FF, 0x1EF, 0xBA};

int patternMask(ActivityCalendarWidget::Category c) {
    switch (c) {
    case ActivityCalendarWidget::Study: return kPatternMask[0];
    case ActivityCalendarWidget::Google: return kPatternMask[1];
    case ActivityCalendarWidget::Local: return kPatternMask[2];
    default: return 0;
    }
}

} // namespace

void ActivityCalendarWidget::paintCategoryPattern(QPainter* painter, Category category,
                                                   const QRectF& box) {
    const int mask = patternMask(category);
    if (mask == 0)
        return;
    const qreal cell = box.width() / 3.0;
    const qreal d = cell * 0.62;
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(pass::theme::kFg);
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < 9; ++i) {
        if (!(mask & (1 << i)))
            continue;
        const int row = i / 3;
        const int col = i % 3;
        const qreal x = box.left() + col * cell + (cell - d) / 2.0;
        const qreal y = box.top() + row * cell + (cell - d) / 2.0;
        painter->drawEllipse(QRectF(x, y, d, d));
    }
    painter->restore();
}

QPixmap ActivityCalendarWidget::categoryPixmap(Category category, int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintCategoryPattern(&p, category, QRectF(0, 0, px, px));
    return pm;
}

void ActivityCalendarWidget::setActivity(const QHash<QDate, Categories>& activity) {
    m_activity = activity;
    updateCells();
}

void ActivityCalendarWidget::paintCell(QPainter* painter, const QRect& rect, QDate date) const {
    // Primero la celda normal (numero, seleccion, dia de hoy...).
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

    // Un patron 3x3 por categoria, centrados horizontalmente bajo el numero.
    const qreal cluster = qBound(9.0, rect.height() / 6.0, 13.0);
    const qreal gap = cluster - 2.0;
    const qreal totalWidth = present.size() * cluster + (present.size() - 1) * gap;
    const qreal cx = rect.left() + rect.width() / 2.0;
    qreal x = cx - totalWidth / 2.0;
    qreal y = rect.top() + rect.height() / 2.0 + painter->fontMetrics().height() / 2.0 + 3.0;
    y = qMin(y, rect.bottom() - cluster - 2.0);

    painter->save();
    for (Category c : present) {
        paintCategoryPattern(painter, c, QRectF(x, y, cluster, cluster));
        x += cluster + gap;
    }
    painter->restore();
}

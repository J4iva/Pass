// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCalendarWidget>
#include <QColor>
#include <QDate>
#include <QHash>
#include <QPixmap>
#include <QRectF>

class QPainter;

// Calendario que marca cada dia con pequenos patrones de DOTS segun el tipo de
// actividad presente (sesion de estudio, evento de Google, evento local). La
// categoria se codifica por PATRON, no por color (lenguaje Nothing / monocromo):
//   Study  -> bloque solido 3x3
//   Google -> anillo hueco 3x3
//   Local  -> cruz 3x3
// Un dia puede mostrar varios patrones si tiene actividades de distinto tipo.
class ActivityCalendarWidget : public QCalendarWidget {
    Q_OBJECT

public:
    enum Category {
        NoActivity = 0x0,
        Study = 0x1,  // sesion de estudio planificada
        Google = 0x2, // evento sincronizado con Google
        Local = 0x4,  // evento local
    };
    Q_DECLARE_FLAGS(Categories, Category)

    using QCalendarWidget::QCalendarWidget;

    // Reemplaza el mapa de actividad por día y repinta.
    void setActivity(const QHash<QDate, Categories>& activity);

    // Pinta el patron de dots de una categoria dentro de `box` (lattice 3x3).
    static void paintCategoryPattern(QPainter* painter, Category category, const QRectF& box);
    // Pixmap del patron (para la leyenda).
    static QPixmap categoryPixmap(Category category, int px);

protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;

private:
    QHash<QDate, Categories> m_activity;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ActivityCalendarWidget::Categories)

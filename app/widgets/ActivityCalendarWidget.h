// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCalendarWidget>
#include <QColor>
#include <QDate>
#include <QHash>

// Calendario que marca cada día con pequeños puntos de color según el tipo de
// actividad presente (sesión de estudio, evento de Google, evento local). El
// número del día se mantiene en su color normal: el diseño es sobrio y los
// puntos solo indican "qué hay" sin saturar la vista. Un día puede mostrar
// varios puntos si tiene actividades de distinto tipo.
class ActivityCalendarWidget : public QCalendarWidget {
    Q_OBJECT

public:
    enum Category {
        NoActivity = 0x0,
        Study = 0x1,  // sesión de estudio planificada (📚)
        Google = 0x2, // evento sincronizado con Google (🌐)
        Local = 0x4,  // evento local
    };
    Q_DECLARE_FLAGS(Categories, Category)

    using QCalendarWidget::QCalendarWidget;

    // Reemplaza el mapa de actividad por día y repinta.
    void setActivity(const QHash<QDate, Categories>& activity);

    // Paleta muted compartida con la leyenda (verde/azul/ámbar).
    static QColor categoryColor(Category category);

protected:
    void paintCell(QPainter* painter, const QRect& rect, QDate date) const override;

private:
    QHash<QDate, Categories> m_activity;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(ActivityCalendarWidget::Categories)

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/calendar/CalendarProvider.h"
#include "pass/repo/EventRepository.h"

namespace pass {

class GoogleSyncService;

// Router de calendario que reemplaza a LocalCalendarProvider en MainWindow sin
// tocar las vistas:
//   - lecturas → repositorio (las filas de Google ya conviven en `events`).
//   - mutaciones de eventos locales → flujo local de siempre.
//   - mutaciones de eventos de Google → write-through: se aplican primero en
//     Google (vía GoogleSyncService) y solo si tiene éxito se reflejan en local;
//     si falla, no se toca lo local y se avisa con syncErrorOccurred.
// Mantiene la interfaz síncrona de CalendarProvider esperando el resultado de
// red con un bucle de eventos anidado (operaciones puntuales del usuario).
class CalendarService : public CalendarProvider {
    Q_OBJECT

public:
    CalendarService(QSqlDatabase db, GoogleSyncService* sync, QObject* parent = nullptr);

    QString providerId() const override { return QStringLiteral("app"); }
    bool canUploadToRemote() const override;
    QList<CalendarEvent> eventsBetween(const QDateTime& fromUtc, const QDateTime& toUtc) override;
    bool addEvent(CalendarEvent& event) override;
    bool updateEvent(const CalendarEvent& event) override;
    bool removeEvent(const QUuid& id) override;

signals:
    // Aviso intrusivo (MainWindow lo muestra en un QMessageBox).
    void syncErrorOccurred(const QString& message);

private:
    bool addLocal(CalendarEvent& event);
    bool addGoogle(CalendarEvent& event);

    EventRepository m_repo;
    GoogleSyncService* m_sync;
};

} // namespace pass

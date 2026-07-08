// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/google/CalendarClient.h"
#include "pass/repo/EventRepository.h"
#include "pass/repo/SyncStateRepository.h"

#include <QDateTime>
#include <QObject>

class QTimer;

namespace pass {

class GoogleAuthService;

// Orquesta la sincronización con Google Calendar:
//   - pull incremental (syncToken) o full (ventana curso académico) con
//     paginación; upsert de la fila espejo por (provider, external_id) y borrado
//     de las canceladas.
//   - push write-through (create/update/delete) para CalendarService.
//   - timer cada 15 min + sync al arrancar + sincronización manual.
// La lógica de red se delega en CalendarClient (testeable con FakeCalendarClient).
class GoogleSyncService : public QObject {
    Q_OBJECT

public:
    enum class Status { Disconnected, Idle, Syncing, Error };
    Q_ENUM(Status)

    GoogleSyncService(QSqlDatabase db, CalendarClient& client, GoogleAuthService& auth,
                      QObject* parent = nullptr);

    Status status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    QDateTime lastSync() const;
    bool isConnected() const; // ¿hay cuenta de Google vinculada?

    // Arranca el timer de 15 min y lanza una primera sincronización si hay cuenta.
    void start();

    using EventCallback = CalendarClient::EventCallback;
    using PlainCallback = CalendarClient::PlainCallback;
    // Write-through: suben el cambio a Google y entregan el resultado por callback.
    void pushCreate(const CalendarEvent& local, EventCallback cb);
    void pushUpdate(const CalendarEvent& local, EventCallback cb);
    void pushDelete(const CalendarEvent& local, PlainCallback cb);

public slots:
    void syncNow(); // pull manual (botón "Sincronizar ahora")

signals:
    void statusChanged(GoogleSyncService::Status status);
    void syncFinished();                        // la UI debe refrescar el calendario
    void errorOccurred(const QString& message); // no intrusivo (visible en Ajustes)

private:
    void pull();
    void fetchPage(const QString& syncToken, const QString& pageToken, bool full);
    void applyPage(const ListPage& page);
    void handlePullError(const ApiError& err);
    void finishPull(const QString& nextSyncToken);
    void setStatus(Status status);
    QPair<QDateTime, QDateTime> fullSyncWindow() const;

    QSqlDatabase m_db; // para transacciones que envuelven los upserts de una página
    EventRepository m_events;
    SyncStateRepository m_state;
    CalendarClient& m_client;
    GoogleAuthService& m_auth;
    QTimer* m_timer = nullptr;
    Status m_status = Status::Disconnected;
    QString m_lastError;
    bool m_pulling = false;       // evita pulls solapados
    bool m_didGoneResync = false; // anti-bucle ante 410 repetidos
    int m_rateLimitRetries = 0;   // backoff 403
};

} // namespace pass

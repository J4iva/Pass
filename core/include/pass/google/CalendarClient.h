// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/domain/CalendarEvent.h"

#include <QObject>
#include <QString>

#include <functional>

namespace pass {

// Resultado de una operación contra la API. `kind` distingue los casos que la
// matriz de errores del plan trata de forma distinta.
struct ApiError {
    enum Kind {
        None,
        Network,       // sin conexión / timeout
        Unauthorized,  // 401: token inválido → refrescar y reintentar
        RateLimited,   // 403 rate limit → backoff
        NotFound,      // 404: el recurso ya no existe en Google
        GoneSyncToken, // 410: syncToken caducado → full resync
        Conflict412,   // 412: If-Match falló (etag desfasado)
        Other
    };
    Kind kind = None;
    QString message;

    bool ok() const { return kind == None; }
};

// Un evento remoto ya mapeado a dominio, con la marca de borrado de Google.
struct RemoteEvent {
    CalendarEvent event;
    bool cancelled = false;
};

// Una página de resultados de list(): eventos + tokens de continuación.
struct ListPage {
    QList<RemoteEvent> events;
    QString nextPageToken; // si no está vacío, hay más páginas
    QString nextSyncToken; // presente en la última página
};

// Interfaz de cliente de calendario remoto. GoogleCalendarClient la implementa
// sobre la API REST; los tests usan FakeCalendarClient. Todas las operaciones
// son asíncronas (callbacks) porque requieren red y refresco de token.
class CalendarClient : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    using ListCallback = std::function<void(ListPage, ApiError)>;
    using EventCallback = std::function<void(RemoteEvent, ApiError)>;
    using PlainCallback = std::function<void(ApiError)>;

    // Lista eventos. Si `syncToken` no está vacío => incremental (Google prohíbe
    // timeMin/timeMax). Si está vacío => full sync con la ventana [timeMin,timeMax].
    virtual void listEvents(const QString& syncToken, const QString& pageToken,
                            const QDateTime& timeMin, const QDateTime& timeMax,
                            ListCallback cb) = 0;
    virtual void insertEvent(const CalendarEvent& event, EventCallback cb) = 0;
    // Actualiza con If-Match sobre el etag del evento (write-through).
    virtual void patchEvent(const CalendarEvent& event, EventCallback cb) = 0;
    virtual void deleteEvent(const QString& externalId, const QString& etag, PlainCallback cb) = 0;
};

} // namespace pass

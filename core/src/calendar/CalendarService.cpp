// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/calendar/CalendarService.h"

#include "pass/google/GoogleSyncService.h"

#include <QEventLoop>
#include <QTimer>

namespace pass {

namespace {

constexpr int kWriteThroughTimeoutMs = 20000; // 20 s de cortesía para la red
const QString kGoogle = QStringLiteral("google");

bool isGoogle(const CalendarEvent& e) {
    return e.provider == kGoogle;
}

} // namespace

CalendarService::CalendarService(QSqlDatabase db, GoogleSyncService* sync, QObject* parent)
    : CalendarProvider(parent), m_repo(db), m_sync(sync) {
    // Cuando una sincronización de fondo cambia las filas espejo, refrescamos la UI.
    if (m_sync)
        connect(m_sync, &GoogleSyncService::syncFinished, this, &CalendarProvider::eventsChanged);
}

bool CalendarService::canUploadToRemote() const {
    return m_sync && m_sync->isConnected();
}

QList<CalendarEvent> CalendarService::eventsBetween(const QDateTime& fromUtc,
                                                    const QDateTime& toUtc) {
    return m_repo.between(fromUtc, toUtc);
}

bool CalendarService::addEvent(CalendarEvent& event) {
    return isGoogle(event) ? addGoogle(event) : addLocal(event);
}

bool CalendarService::addLocal(CalendarEvent& event) {
    if (event.id.isNull())
        event.id = QUuid::createUuid();
    event.provider = QStringLiteral("local");
    if (!m_repo.add(event))
        return false;
    emit eventsChanged();
    return true;
}

bool CalendarService::addGoogle(CalendarEvent& event) {
    if (!m_sync) {
        emit syncErrorOccurred(tr("La sincronización con Google no está disponible."));
        return false;
    }
    if (event.id.isNull())
        event.id = QUuid::createUuid();

    // Write-through síncrono: subimos a Google y esperamos el resultado.
    bool finished = false;
    ApiError err;
    RemoteEvent remote;
    QEventLoop loop;
    m_sync->pushCreate(event, [&](RemoteEvent re, ApiError e) {
        remote = re;
        err = e;
        finished = true;
        loop.quit();
    });
    if (!finished) {
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, [&] {
            err = {ApiError::Network, tr("Tiempo de espera agotado.")};
            loop.quit();
        });
        timer.start(kWriteThroughTimeoutMs);
        loop.exec();
    }

    if (!err.ok()) {
        emit syncErrorOccurred(tr("Sin conexión: el evento no se creó en Google."));
        return false; // revert: no se guarda nada en local
    }

    // Éxito: guardamos la fila espejo con los datos que devolvió Google.
    event.provider = kGoogle;
    event.externalId = remote.event.externalId;
    event.etag = remote.event.etag;
    if (remote.event.updatedAt.isValid())
        event.updatedAt = remote.event.updatedAt;
    if (!m_repo.add(event))
        return false;
    emit eventsChanged();
    return true;
}

bool CalendarService::updateEvent(const CalendarEvent& event) {
    if (!isGoogle(event)) {
        if (!m_repo.update(event))
            return false;
        emit eventsChanged();
        return true;
    }
    if (!m_sync) {
        emit syncErrorOccurred(tr("La sincronización con Google no está disponible."));
        return false;
    }

    bool finished = false;
    ApiError err;
    RemoteEvent remote;
    QEventLoop loop;
    m_sync->pushUpdate(event, [&](RemoteEvent re, ApiError e) {
        remote = re;
        err = e;
        finished = true;
        loop.quit();
    });
    if (!finished) {
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, [&] {
            err = {ApiError::Network, tr("Tiempo de espera agotado.")};
            loop.quit();
        });
        timer.start(kWriteThroughTimeoutMs);
        loop.exec();
    }

    if (err.ok()) {
        CalendarEvent updated = event;
        updated.etag = remote.event.etag;
        if (remote.event.updatedAt.isValid())
            updated.updatedAt = remote.event.updatedAt;
        if (!m_repo.update(updated))
            return false;
        emit eventsChanged();
        return true;
    }

    // Matriz de errores (write-through).
    switch (err.kind) {
    case ApiError::Conflict412:
        // El evento cambió en Google: descartamos el cambio local y re-pull.
        if (m_sync)
            m_sync->syncNow();
        emit syncErrorOccurred(
            tr("El evento se modificó en Google; se conservó la versión de Google."));
        break;
    case ApiError::NotFound:
        // Ya no existe en Google: eliminamos la fila espejo local.
        m_repo.remove(event.id);
        emit eventsChanged();
        emit syncErrorOccurred(tr("El evento ya no existe en Google; se ha eliminado."));
        break;
    default:
        emit syncErrorOccurred(tr("Sin conexión: el cambio no se aplicó."));
        break;
    }
    return false;
}

bool CalendarService::removeEvent(const QUuid& id) {
    const auto existing = m_repo.byId(id);
    if (!existing)
        return false;

    if (!isGoogle(*existing)) {
        if (!m_repo.remove(id))
            return false;
        emit eventsChanged();
        return true;
    }
    if (!m_sync) {
        emit syncErrorOccurred(tr("La sincronización con Google no está disponible."));
        return false;
    }

    // Borrado de evento de Google: remoto primero, local después.
    bool finished = false;
    ApiError err;
    QEventLoop loop;
    m_sync->pushDelete(*existing, [&](ApiError e) {
        err = e;
        finished = true;
        loop.quit();
    });
    if (!finished) {
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, [&] {
            err = {ApiError::Network, tr("Tiempo de espera agotado.")};
            loop.quit();
        });
        timer.start(kWriteThroughTimeoutMs);
        loop.exec();
    }

    // 404 => ya estaba borrado en Google: igualmente quitamos la fila local.
    if (err.ok() || err.kind == ApiError::NotFound) {
        if (!m_repo.remove(id))
            return false;
        emit eventsChanged();
        return true;
    }

    emit syncErrorOccurred(tr("Sin conexión: no se pudo eliminar en Google."));
    return false;
}

} // namespace pass

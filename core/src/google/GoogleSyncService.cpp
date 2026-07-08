// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/GoogleSyncService.h"

#include "pass/google/GoogleAuthService.h"

#include <QTimeZone>
#include <QTimer>

namespace pass {

namespace {

constexpr int kSyncIntervalMs = 15 * 60 * 1000; // 15 min
constexpr int kFullSyncPastDays = 90;
constexpr int kFullSyncFutureDays = 365;
constexpr int kMaxRateLimitRetries = 3;

QString nowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

} // namespace

GoogleSyncService::GoogleSyncService(QSqlDatabase db, CalendarClient& client,
                                     GoogleAuthService& auth, QObject* parent)
    : QObject(parent), m_db(db), m_events(db), m_state(db), m_client(client), m_auth(auth) {}

bool GoogleSyncService::isConnected() const {
    return m_auth.isConnected();
}

QDateTime GoogleSyncService::lastSync() const {
    const auto raw = m_state.get(QString::fromLatin1(SyncStateRepository::kLastSync));
    if (!raw || raw->isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(*raw, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

QPair<QDateTime, QDateTime> GoogleSyncService::fullSyncWindow() const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return {now.addDays(-kFullSyncPastDays), now.addDays(kFullSyncFutureDays)};
}

void GoogleSyncService::start() {
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kSyncIntervalMs);
        connect(m_timer, &QTimer::timeout, this, &GoogleSyncService::syncNow);
    }
    m_timer->start();
    if (m_auth.isConnected())
        pull();
    else
        setStatus(Status::Disconnected);
}

void GoogleSyncService::syncNow() {
    pull();
}

void GoogleSyncService::setStatus(Status status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(m_status);
}

void GoogleSyncService::pull() {
    if (!m_auth.isConnected()) {
        setStatus(Status::Disconnected);
        return;
    }
    if (m_pulling)
        return;

    m_pulling = true;
    setStatus(Status::Syncing);
    const QString syncToken =
        m_state.get(QString::fromLatin1(SyncStateRepository::kSyncToken)).value_or(QString());
    fetchPage(syncToken, QString(), syncToken.isEmpty());
}

void GoogleSyncService::fetchPage(const QString& syncToken, const QString& pageToken, bool full) {
    QDateTime timeMin;
    QDateTime timeMax;
    if (full) {
        const auto window = fullSyncWindow();
        timeMin = window.first;
        timeMax = window.second;
    }

    m_client.listEvents(syncToken, pageToken, timeMin, timeMax,
                        [this, syncToken, full](ListPage page, ApiError err) {
                            if (!err.ok()) {
                                handlePullError(err);
                                return;
                            }
                            m_rateLimitRetries = 0; // hubo progreso
                            applyPage(page);
                            if (!page.nextPageToken.isEmpty())
                                fetchPage(syncToken, page.nextPageToken, full);
                            else
                                finishPull(page.nextSyncToken);
                        });
}

void GoogleSyncService::applyPage(const ListPage& page) {
    const QString provider = QStringLiteral("google");
    m_db.transaction();
    for (const RemoteEvent& re : page.events) {
        if (re.event.externalId.isEmpty())
            continue;
        if (re.cancelled) {
            m_events.removeByExternalId(provider, re.event.externalId);
            continue;
        }
        CalendarEvent e = re.event;
        // Conserva los metadatos locales que Google no conoce (id, asignatura,
        // sesión de estudio que originó el evento).
        if (const auto existing = m_events.byExternalId(provider, e.externalId)) {
            e.id = existing->id;
            if (e.subjectId.isNull())
                e.subjectId = existing->subjectId;
            e.sourceSessionId = existing->sourceSessionId;
        }
        m_events.upsertByExternalId(e);
    }
    m_db.commit();
}

void GoogleSyncService::finishPull(const QString& nextSyncToken) {
    if (!nextSyncToken.isEmpty())
        m_state.set(QString::fromLatin1(SyncStateRepository::kSyncToken), nextSyncToken);
    m_state.set(QString::fromLatin1(SyncStateRepository::kLastSync), nowIso());

    m_pulling = false;
    m_didGoneResync = false;
    m_lastError.clear();
    setStatus(Status::Idle);
    emit syncFinished();
}

void GoogleSyncService::handlePullError(const ApiError& err) {
    m_pulling = false;

    switch (err.kind) {
    case ApiError::GoneSyncToken:
        // 410: el syncToken caducó → full resync inmediato (una sola vez).
        m_state.remove(QString::fromLatin1(SyncStateRepository::kSyncToken));
        if (!m_didGoneResync) {
            m_didGoneResync = true;
            pull();
            return;
        }
        m_lastError = tr("No se pudo resincronizar con Google.");
        break;

    case ApiError::Unauthorized:
        // El cliente ya hizo refresh + 1 reintento; si seguimos aquí, hay que
        // volver a conectar la cuenta.
        m_lastError = tr("Sesión de Google caducada. Vuelve a conectar tu cuenta.");
        break;

    case ApiError::RateLimited:
        if (m_rateLimitRetries < kMaxRateLimitRetries) {
            const int delayMs = 1000 * (1 << m_rateLimitRetries); // 1s, 2s, 4s
            ++m_rateLimitRetries;
            QTimer::singleShot(delayMs, this, [this] { pull(); });
            return;
        }
        m_rateLimitRetries = 0;
        m_lastError = tr("Google está limitando las peticiones; reintento en 15 min.");
        break;

    case ApiError::Network:
        // Se salta en silencio; el estado queda visible en Ajustes.
        m_lastError = tr("Sin conexión con Google.");
        break;

    default:
        m_lastError = err.message;
        break;
    }

    setStatus(Status::Error);
    emit errorOccurred(m_lastError);
}

void GoogleSyncService::pushCreate(const CalendarEvent& local, EventCallback cb) {
    m_client.insertEvent(local, std::move(cb));
}

void GoogleSyncService::pushUpdate(const CalendarEvent& local, EventCallback cb) {
    m_client.patchEvent(local, std::move(cb));
}

void GoogleSyncService::pushDelete(const CalendarEvent& local, PlainCallback cb) {
    m_client.deleteEvent(local.externalId, local.etag, std::move(cb));
}

} // namespace pass

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/google/CalendarClient.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace pass {

class GoogleAuthService;

// Cliente real contra la API de Google Calendar v3 (calendario "primary").
// Obtiene un access_token fresco vía GoogleAuthService antes de cada petición
// y reintenta una vez ante un 401 (refresh + retry). Endpoints HTTPS fijos.
class GoogleCalendarClient : public CalendarClient {
    Q_OBJECT

public:
    GoogleCalendarClient(GoogleAuthService& auth, QObject* parent = nullptr);

    void listEvents(const QString& syncToken, const QString& pageToken, const QDateTime& timeMin,
                    const QDateTime& timeMax, ListCallback cb) override;
    void insertEvent(const CalendarEvent& event, EventCallback cb) override;
    void patchEvent(const CalendarEvent& event, EventCallback cb) override;
    void deleteEvent(const QString& externalId, const QString& etag, PlainCallback cb) override;

private:
    // Ejecuta una petición autenticada; reintenta una vez si llega 401.
    void sendAuthed(const std::function<QNetworkReply*(const QString& token)>& build,
                    const std::function<void(QNetworkReply*, ApiError)>& done,
                    bool isRetry = false);

    GoogleAuthService& m_auth;
    QNetworkAccessManager* m_nam;
};

} // namespace pass

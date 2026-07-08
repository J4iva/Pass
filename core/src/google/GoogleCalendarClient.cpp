// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/GoogleCalendarClient.h"

#include "pass/google/GoogleAuthService.h"
#include "pass/google/GoogleEventMapper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace pass {

namespace {

const QString
    kEventsUrl(QStringLiteral("https://www.googleapis.com/calendar/v3/calendars/primary/events"));

ApiError mapReply(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError)
        return {};
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // errorString no contiene el token (solo va en la cabecera Authorization).
    const QString msg = QStringLiteral("HTTP %1: %2").arg(http).arg(reply->errorString());
    switch (http) {
    case 401:
        return {ApiError::Unauthorized, msg};
    case 403:
        return {ApiError::RateLimited, msg};
    case 404:
        return {ApiError::NotFound, msg};
    case 410:
        return {ApiError::GoneSyncToken, msg};
    case 412:
        return {ApiError::Conflict412, msg};
    case 0:
        return {ApiError::Network, reply->errorString()};
    default:
        return {ApiError::Other, msg};
    }
}

QByteArray toBody(const QJsonObject& obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QNetworkRequest jsonRequest(const QUrl& url, const QString& token) {
    QNetworkRequest req(url);
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    return req;
}

} // namespace

GoogleCalendarClient::GoogleCalendarClient(GoogleAuthService& auth, QObject* parent)
    : CalendarClient(parent), m_auth(auth), m_nam(new QNetworkAccessManager(this)) {}

void GoogleCalendarClient::sendAuthed(const std::function<QNetworkReply*(const QString&)>& build,
                                      const std::function<void(QNetworkReply*, ApiError)>& done,
                                      bool isRetry) {
    m_auth.withFreshToken(
        [this, build, done, isRetry](const QString& token) {
            QNetworkReply* reply = build(token);
            connect(reply, &QNetworkReply::finished, this, [this, reply, build, done, isRetry] {
                const ApiError err = mapReply(reply);
                // 401: refresh + 1 reintento (matriz de errores del plan).
                if (err.kind == ApiError::Unauthorized && !isRetry) {
                    reply->deleteLater();
                    m_auth.invalidateAccessToken();
                    sendAuthed(build, done, /*isRetry=*/true);
                    return;
                }
                done(reply, err);
                reply->deleteLater();
            });
        },
        [done](const QString& message) {
            // No se pudo obtener token (refresh fallido): se trata como 401.
            done(nullptr, {ApiError::Unauthorized, message});
        });
}

void GoogleCalendarClient::listEvents(const QString& syncToken, const QString& pageToken,
                                      const QDateTime& timeMin, const QDateTime& timeMax,
                                      ListCallback cb) {
    sendAuthed(
        [&, syncToken, pageToken, timeMin, timeMax](const QString& token) -> QNetworkReply* {
            QUrl url(kEventsUrl);
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("singleEvents"), QStringLiteral("true"));
            q.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("250"));
            if (!pageToken.isEmpty())
                q.addQueryItem(QStringLiteral("pageToken"), pageToken);
            if (!syncToken.isEmpty()) {
                // Incremental: Google prohíbe timeMin/timeMax junto a syncToken.
                q.addQueryItem(QStringLiteral("syncToken"), syncToken);
                q.addQueryItem(QStringLiteral("showDeleted"), QStringLiteral("true"));
            } else {
                q.addQueryItem(QStringLiteral("timeMin"), timeMin.toUTC().toString(Qt::ISODate));
                q.addQueryItem(QStringLiteral("timeMax"), timeMax.toUTC().toString(Qt::ISODate));
            }
            url.setQuery(q);
            return m_nam->get(jsonRequest(url, token));
        },
        [cb](QNetworkReply* reply, ApiError err) {
            if (!err.ok() || !reply) {
                cb({}, err);
                return;
            }
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            ListPage page;
            page.nextPageToken = root.value(QStringLiteral("nextPageToken")).toString();
            page.nextSyncToken = root.value(QStringLiteral("nextSyncToken")).toString();
            for (const QJsonValue& v : root.value(QStringLiteral("items")).toArray()) {
                RemoteEvent re;
                re.event = GoogleEventMapper::fromJson(v.toObject(), &re.cancelled);
                page.events.append(re);
            }
            cb(page, {});
        });
}

void GoogleCalendarClient::insertEvent(const CalendarEvent& event, EventCallback cb) {
    const QJsonObject body = GoogleEventMapper::toJson(event);
    sendAuthed(
        [&, body](const QString& token) -> QNetworkReply* {
            return m_nam->post(jsonRequest(QUrl(kEventsUrl), token), toBody(body));
        },
        [cb](QNetworkReply* reply, ApiError err) {
            if (!err.ok() || !reply) {
                cb({}, err);
                return;
            }
            RemoteEvent re;
            re.event = GoogleEventMapper::fromJson(
                QJsonDocument::fromJson(reply->readAll()).object(), &re.cancelled);
            cb(re, {});
        });
}

void GoogleCalendarClient::patchEvent(const CalendarEvent& event, EventCallback cb) {
    const QJsonObject body = GoogleEventMapper::toJson(event);
    const QString externalId = event.externalId;
    const QString etag = event.etag;
    sendAuthed(
        [&, body, externalId, etag](const QString& token) -> QNetworkReply* {
            QNetworkRequest req =
                jsonRequest(QUrl(kEventsUrl + QLatin1Char('/') + externalId), token);
            if (!etag.isEmpty())
                req.setRawHeader(QByteArrayLiteral("If-Match"), etag.toUtf8());
            return m_nam->sendCustomRequest(req, QByteArrayLiteral("PATCH"), toBody(body));
        },
        [cb](QNetworkReply* reply, ApiError err) {
            if (!err.ok() || !reply) {
                cb({}, err);
                return;
            }
            RemoteEvent re;
            re.event = GoogleEventMapper::fromJson(
                QJsonDocument::fromJson(reply->readAll()).object(), &re.cancelled);
            cb(re, {});
        });
}

void GoogleCalendarClient::deleteEvent(const QString& externalId, const QString& etag,
                                       PlainCallback cb) {
    sendAuthed(
        [&, externalId, etag](const QString& token) -> QNetworkReply* {
            QNetworkRequest req =
                jsonRequest(QUrl(kEventsUrl + QLatin1Char('/') + externalId), token);
            if (!etag.isEmpty())
                req.setRawHeader(QByteArrayLiteral("If-Match"), etag.toUtf8());
            return m_nam->deleteResource(req);
        },
        [cb](QNetworkReply*, ApiError err) { cb(err); });
}

} // namespace pass

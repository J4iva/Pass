// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/GoogleAuthService.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QTimeZone>
#include <QUrlQuery>

namespace pass {

namespace {

// Endpoints de Google hardcodeados sobre HTTPS (TLS por schannel en Windows).
const QUrl kAuthUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
const QUrl kTokenUrl(QStringLiteral("https://oauth2.googleapis.com/token"));
const QUrl kRevokeUrl(QStringLiteral("https://oauth2.googleapis.com/revoke"));

// Scope mínimo: solo eventos de calendario (ni lectura de perfil ni nada más).
const QByteArray kScope("https://www.googleapis.com/auth/calendar.events");

constexpr int kFreshnessMarginSecs = 60; // refrescar si caduca en <60 s

} // namespace

GoogleAuthService::GoogleAuthService(TokenStore& store, QObject* parent)
    : QObject(parent), m_store(store), m_nam(new QNetworkAccessManager(this)) {
    if (hasClientCredentials())
        buildFlow();
}

GoogleAuthService::~GoogleAuthService() = default;

bool GoogleAuthService::hasClientCredentials() const {
    const auto id = m_store.read(TokenStore::kClientId);
    return id.has_value() && !id->isEmpty();
}

bool GoogleAuthService::isConnected() const {
    const auto rt = m_store.read(TokenStore::kRefreshToken);
    return rt.has_value() && !rt->isEmpty();
}

void GoogleAuthService::setClientCredentials(const QString& clientId, const QString& clientSecret) {
    m_store.write(TokenStore::kClientId, clientId.trimmed());
    m_store.write(TokenStore::kClientSecret, clientSecret.trimmed());
    // Reconstruye el flujo para que tome las nuevas credenciales.
    if (m_flow) {
        m_flow->deleteLater();
        m_flow = nullptr;
    }
    buildFlow();
}

void GoogleAuthService::buildFlow() {
    const auto clientId = m_store.read(TokenStore::kClientId).value_or(QString());
    const auto clientSecret = m_store.read(TokenStore::kClientSecret).value_or(QString());
    if (clientId.isEmpty())
        return;

    m_flow = new QOAuth2AuthorizationCodeFlow(m_nam, this);
    m_flow->setAuthorizationUrl(kAuthUrl);
    m_flow->setTokenUrl(kTokenUrl);
    m_flow->setClientIdentifier(clientId);
    m_flow->setClientIdentifierSharedKey(clientSecret);
    m_flow->setRequestedScopeTokens({kScope});
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

    // access_type=offline + prompt=consent: imprescindibles para recibir un
    // refresh_token de Google (y volver a recibirlo en cada reconexión).
    m_flow->setModifyParametersFunction(
        [](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant>* params) {
            if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
                params->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
                params->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
            }
        });

    // No abrimos el navegador desde el core (sin QtGui): lo reenviamos a la UI.
    connect(m_flow, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser, this,
            &GoogleAuthService::openAuthorizationUrl);
    connect(m_flow, &QOAuth2AuthorizationCodeFlow::granted, this, &GoogleAuthService::onGranted);
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString& error, const QString& desc, const QUrl&) {
                onAuthFailed(desc.isEmpty() ? error : QStringLiteral("%1: %2").arg(error, desc));
            });
    connect(m_flow, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error) {
        onAuthFailed(tr("Error de red al hablar con Google."));
    });
}

void GoogleAuthService::connectAccount() {
    if (!m_flow)
        buildFlow();
    if (!m_flow) {
        emit authError(tr("Falta el client_id de Google. Configúralo en Ajustes."));
        return;
    }

    // El servidor de respuesta escucha SOLO en 127.0.0.1 con puerto efímero, y
    // solo durante el flujo de autorización (se cierra al terminar).
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_reply = new QOAuthHttpServerReplyHandler(QHostAddress::LocalHost, 0, this);
    m_reply->setCallbackHost(QStringLiteral("127.0.0.1"));
    if (!m_reply->isListening()) {
        m_reply->deleteLater();
        m_reply = nullptr;
        emit authError(tr("No se pudo abrir el puerto local para la autorización."));
        return;
    }
    m_flow->setReplyHandler(m_reply);

    m_connecting = true;
    m_flow->grant();
}

void GoogleAuthService::onGranted() {
    persistTokens();

    // Cierra el servidor loopback: ya no hace falta tras obtener el token.
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_flow)
            m_flow->setReplyHandler(nullptr);
    }

    if (m_connecting) {
        m_connecting = false;
        emit connected();
    }
    if (m_refreshing) {
        m_refreshing = false;
        const QString access = storedAccessToken();
        const auto pending = std::exchange(m_pending, {});
        for (const auto& p : pending)
            p.ok(access);
    }
}

void GoogleAuthService::onAuthFailed(const QString& message) {
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_flow)
            m_flow->setReplyHandler(nullptr);
    }

    if (m_connecting) {
        m_connecting = false;
        emit authError(message);
    }

    if (m_refreshing) {
        m_refreshing = false;
        // invalid_grant => el refresh_token ya no sirve: la cuenta queda desligada.
        if (message.contains(QStringLiteral("invalid_grant"), Qt::CaseInsensitive)) {
            m_store.remove(TokenStore::kAccessToken);
            m_store.remove(TokenStore::kRefreshToken);
            m_store.remove(TokenStore::kTokenExpiry);
            emit disconnected();
        }
        const auto pending = std::exchange(m_pending, {});
        for (const auto& p : pending)
            p.fail(message);
    }
}

void GoogleAuthService::persistTokens() {
    if (!m_flow)
        return;
    const QString access = m_flow->token();
    if (!access.isEmpty())
        m_store.write(TokenStore::kAccessToken, access);

    // El refresh_token solo llega en algunas respuestas; no machacar con vacío.
    const QString refresh = m_flow->refreshToken();
    if (!refresh.isEmpty())
        m_store.write(TokenStore::kRefreshToken, refresh);

    const QDateTime expiry = m_flow->expirationAt();
    if (expiry.isValid())
        m_store.write(TokenStore::kTokenExpiry, expiry.toUTC().toString(Qt::ISODate));
}

QString GoogleAuthService::storedAccessToken() const {
    return m_store.read(TokenStore::kAccessToken).value_or(QString());
}

QDateTime GoogleAuthService::storedExpiry() const {
    const auto raw = m_store.read(TokenStore::kTokenExpiry);
    if (!raw || raw->isEmpty())
        return {};
    QDateTime dt = QDateTime::fromString(*raw, Qt::ISODate);
    dt.setTimeZone(QTimeZone::utc());
    return dt;
}

bool GoogleAuthService::tokenIsFresh() const {
    if (storedAccessToken().isEmpty())
        return false;
    const QDateTime expiry = storedExpiry();
    if (!expiry.isValid())
        return false;
    return QDateTime::currentDateTimeUtc().addSecs(kFreshnessMarginSecs) < expiry;
}

void GoogleAuthService::withFreshToken(TokenCallback onReady, FailCallback onFail) {
    if (!isConnected()) {
        onFail(tr("No hay ninguna cuenta de Google conectada."));
        return;
    }
    if (tokenIsFresh()) {
        onReady(storedAccessToken());
        return;
    }

    m_pending.append({std::move(onReady), std::move(onFail)});
    if (m_refreshing)
        return;

    if (!m_flow)
        buildFlow();
    if (!m_flow) {
        const auto pending = std::exchange(m_pending, {});
        for (const auto& p : pending)
            p.fail(tr("Falta el client_id de Google. Configúralo en Ajustes."));
        return;
    }

    m_refreshing = true;
    m_flow->setRefreshToken(m_store.read(TokenStore::kRefreshToken).value_or(QString()));
    m_flow->refreshTokens();
}

void GoogleAuthService::invalidateAccessToken() {
    m_store.remove(TokenStore::kAccessToken);
    m_store.remove(TokenStore::kTokenExpiry);
}

void GoogleAuthService::disconnectAccount() {
    if (const auto rt = m_store.read(TokenStore::kRefreshToken); rt && !rt->isEmpty())
        revokeRefreshToken(*rt);

    m_store.remove(TokenStore::kAccessToken);
    m_store.remove(TokenStore::kRefreshToken);
    m_store.remove(TokenStore::kTokenExpiry);
    // client_id/secret se conservan para poder reconectar sin reconfigurar.
    emit disconnected();
}

void GoogleAuthService::revokeRefreshToken(const QString& refreshToken) {
    QNetworkRequest req(kRevokeUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QByteArrayLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("token"), refreshToken);
    QNetworkReply* reply = m_nam->post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    // Best-effort: liberamos la respuesta sin volcar el token a ningún log.
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

} // namespace pass

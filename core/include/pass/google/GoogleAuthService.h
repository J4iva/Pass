// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/google/TokenStore.h"

#include <QDateTime>
#include <QObject>
#include <QUrl>

#include <functional>

class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;
class QNetworkAccessManager;

namespace pass {

// Gestiona el OAuth2 de Google Calendar con Authorization Code + PKCE S256.
// Decisiones de seguridad (modo contingencia):
//   - PKCE S256 obligatorio; redirect en loopback 127.0.0.1 con puerto efímero.
//   - Scope mínimo: calendar.events.
//   - Tokens y client_id/secret SOLO en el TokenStore (Administrador de
//     credenciales de Windows); nunca en QSettings, ficheros, el repo ni logs.
//   - No depende de QtGui/Widgets: en vez de abrir el navegador, emite
//     openAuthorizationUrl(); MainWindow lo conecta a QDesktopServices.
class GoogleAuthService : public QObject {
    Q_OBJECT

public:
    explicit GoogleAuthService(TokenStore& store, QObject* parent = nullptr);
    ~GoogleAuthService() override;

    // ¿Hay client_id/secret guardados? (requisito para poder conectar)
    bool hasClientCredentials() const;
    // ¿Hay un refresh_token guardado? (cuenta vinculada)
    bool isConnected() const;

    // Guarda client_id/secret del usuario (su OAuth Client "Desktop" de Google).
    void setClientCredentials(const QString& clientId, const QString& clientSecret);

    // Inicia el flujo de autorización (abre el navegador vía openAuthorizationUrl).
    void connectAccount();
    // Revoca el refresh_token en Google y borra todos los tokens locales.
    void disconnectAccount();

    using TokenCallback = std::function<void(const QString& accessToken)>;
    using FailCallback = std::function<void(const QString& message)>;
    // Entrega un access_token válido a `onReady` (refresca si caduca en <60 s).
    // Si no hay cuenta o el refresco falla, llama a `onFail`.
    void withFreshToken(TokenCallback onReady, FailCallback onFail);

    // Marca el access_token como caducado (tras un 401): el próximo
    // withFreshToken forzará un refresh. No toca el refresh_token.
    void invalidateAccessToken();

signals:
    // MainWindow lo conecta a QDesktopServices::openUrl (passcore no usa QtGui).
    void openAuthorizationUrl(const QUrl& url);
    void connected();
    void disconnected();
    void authError(const QString& message);

private:
    void buildFlow();
    void onGranted();
    void onAuthFailed(const QString& message);
    void persistTokens();
    QString storedAccessToken() const;
    QDateTime storedExpiry() const;
    bool tokenIsFresh() const;
    void revokeRefreshToken(const QString& refreshToken);

    struct Pending {
        TokenCallback ok;
        FailCallback fail;
    };

    TokenStore& m_store;
    QNetworkAccessManager* m_nam = nullptr;
    QOAuth2AuthorizationCodeFlow* m_flow = nullptr;
    QOAuthHttpServerReplyHandler* m_reply = nullptr;
    QList<Pending> m_pending;
    bool m_refreshing = false;
    bool m_connecting = false;
};

} // namespace pass

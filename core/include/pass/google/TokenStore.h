// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <optional>

namespace pass {

// Almacén seguro de secretos para la integración con Google Calendar.
// Interfaz pura: la implementación real (WinCredTokenStore) usa el Administrador
// de credenciales de Windows. NUNCA se guardan en QSettings, ficheros, el repo
// ni los logs. Los tests pueden inyectar una implementación en memoria.
class TokenStore {
public:
    virtual ~TokenStore() = default;

    // Devuelve nullopt si la clave no existe.
    virtual std::optional<QString> read(const QString& key) const = 0;
    virtual bool write(const QString& key, const QString& value) = 0;
    virtual bool remove(const QString& key) = 0;

    // Claves convencionales (TargetName real = "Pass/<clave>").
    static constexpr auto kClientId = "google/client_id";
    static constexpr auto kClientSecret = "google/client_secret";
    static constexpr auto kAccessToken = "google/access_token";
    static constexpr auto kRefreshToken = "google/refresh_token";
    static constexpr auto kTokenExpiry = "google/token_expiry"; // ISO 8601 UTC
};

} // namespace pass

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace pass {

// Almacén clave-valor sobre la tabla `sync_state`. Guarda el syncToken y la
// marca de última sincronización junto a las filas espejo (misma vida y commit
// atómico con los upserts), por eso vive en la DB y no en QSettings.
class SyncStateRepository {
public:
    explicit SyncStateRepository(QSqlDatabase db);

    std::optional<QString> get(const QString& key) const;
    bool set(const QString& key, const QString& value);
    bool remove(const QString& key);

    // Claves convencionales.
    static constexpr auto kSyncToken = "google/sync_token";
    static constexpr auto kLastSync = "google/last_sync"; // ISO 8601 UTC

    // Sincronización entre dispositivos (repo de GitHub).
    static constexpr auto kGithubLastSync = "github/last_sync"; // ISO 8601 UTC

private:
    QSqlDatabase m_db;
};

} // namespace pass

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSettings>
#include <QString>

namespace pass {

// Acceso tipado a la configuración persistente de la app (QSettings).
class AppSettings {
public:
    QString vaultPath() const;
    void setVaultPath(const QString& path);

    // Subcarpeta del vault donde la app escribe sus notas (default "Pass").
    QString vaultSubfolder() const;
    void setVaultSubfolder(const QString& name);

    // Días que abarca la lista de próximos eventos del dashboard (default 7).
    int dashboardDays() const;
    void setDashboardDays(int days);

    // --- Sincronización entre dispositivos (repo de GitHub) ---
    // Ruta del clon local del repo de sync (vacía = no configurado).
    QString syncRepoPath() const;
    void setSyncRepoPath(const QString& path);

    // Rama del repo de sync (default "main"); se detecta al conectar.
    QString syncBranch() const;
    void setSyncBranch(const QString& branch);

    // Identificador estable de este dispositivo (UUID autogenerado al 1er uso).
    QString syncDeviceId();

    // Nombre legible del dispositivo (default = nombre de host).
    QString syncDeviceName() const;
    void setSyncDeviceName(const QString& name);

    // --- CLI por comando (carpeta command/ del repo de sync) ---
    // Procesar comandos `Pass create ...` escritos en command/*.passcmd al hacer
    // pull / arrancar la app. Default true; se puede desactivar sin desinstalar.
    bool commandsEnabled() const;
    void setCommandsEnabled(bool enabled);

private:
    mutable QSettings m_settings;
};

} // namespace pass

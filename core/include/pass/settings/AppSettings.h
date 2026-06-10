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

private:
    mutable QSettings m_settings;
};

} // namespace pass

// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/settings/AppSettings.h"

namespace pass {

namespace {
const auto kVaultPath = "vault/path";
const auto kVaultSubfolder = "vault/subfolder";
}

QString AppSettings::vaultPath() const {
    return m_settings.value(QLatin1String(kVaultPath)).toString();
}

void AppSettings::setVaultPath(const QString& path) {
    m_settings.setValue(QLatin1String(kVaultPath), path);
}

QString AppSettings::vaultSubfolder() const {
    return m_settings.value(QLatin1String(kVaultSubfolder), QStringLiteral("Pass")).toString();
}

void AppSettings::setVaultSubfolder(const QString& name) {
    m_settings.setValue(QLatin1String(kVaultSubfolder), name);
}

} // namespace pass

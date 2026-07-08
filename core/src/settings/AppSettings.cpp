// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/settings/AppSettings.h"

#include <QSysInfo>
#include <QUuid>

namespace pass {

namespace {
const auto kVaultPath = "vault/path";
const auto kVaultSubfolder = "vault/subfolder";
const auto kDashboardDays = "dashboard/days";
const auto kSyncRepoPath = "sync/repoPath";
const auto kSyncBranch = "sync/branch";
const auto kSyncDeviceId = "sync/deviceId";
const auto kSyncDeviceName = "sync/deviceName";
const auto kCommandsEnabled = "commands/enabled";
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

int AppSettings::dashboardDays() const {
    const int days = m_settings.value(QLatin1String(kDashboardDays), 7).toInt();
    return qBound(1, days, 60);
}

void AppSettings::setDashboardDays(int days) {
    m_settings.setValue(QLatin1String(kDashboardDays), qBound(1, days, 60));
}

QString AppSettings::syncRepoPath() const {
    return m_settings.value(QLatin1String(kSyncRepoPath)).toString();
}

void AppSettings::setSyncRepoPath(const QString& path) {
    m_settings.setValue(QLatin1String(kSyncRepoPath), path);
}

QString AppSettings::syncBranch() const {
    return m_settings.value(QLatin1String(kSyncBranch), QStringLiteral("main")).toString();
}

void AppSettings::setSyncBranch(const QString& branch) {
    m_settings.setValue(QLatin1String(kSyncBranch), branch);
}

QString AppSettings::syncDeviceId() {
    QString id = m_settings.value(QLatin1String(kSyncDeviceId)).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings.setValue(QLatin1String(kSyncDeviceId), id);
    }
    return id;
}

QString AppSettings::syncDeviceName() const {
    const QString name = m_settings.value(QLatin1String(kSyncDeviceName)).toString();
    return name.isEmpty() ? QSysInfo::machineHostName() : name;
}

void AppSettings::setSyncDeviceName(const QString& name) {
    m_settings.setValue(QLatin1String(kSyncDeviceName), name);
}

bool AppSettings::commandsEnabled() const {
    // Default true: la feature está activa salvo que el usuario la desactive.
    return m_settings.value(QLatin1String(kCommandsEnabled), true).toBool();
}

void AppSettings::setCommandsEnabled(bool enabled) {
    m_settings.setValue(QLatin1String(kCommandsEnabled), enabled);
}

} // namespace pass

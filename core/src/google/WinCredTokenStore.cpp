// SPDX-License-Identifier: GPL-3.0-or-later
#include "pass/google/WinCredTokenStore.h"

#include <QByteArray>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
// El orden importa: <windows.h> debe ir antes que <wincred.h> (define sus tipos).
#include <windows.h>
#include <wincred.h>
// clang-format on

namespace pass {

namespace {

// Borra de memoria el contenido sensible para reducir su exposición.
void wipe(QByteArray& bytes) {
    if (!bytes.isEmpty())
        SecureZeroMemory(bytes.data(), static_cast<size_t>(bytes.size()));
}

} // namespace

WinCredTokenStore::WinCredTokenStore(QString prefix) : m_prefix(std::move(prefix)) {}

QString WinCredTokenStore::targetName(const QString& key) const {
    return m_prefix + QLatin1Char('/') + key;
}

std::optional<QString> WinCredTokenStore::read(const QString& key) const {
    const std::wstring target = targetName(key).toStdWString();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred))
        return std::nullopt;

    std::optional<QString> result;
    if (cred->CredentialBlob && cred->CredentialBlobSize > 0) {
        result = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob),
                                   static_cast<int>(cred->CredentialBlobSize));
        // Borra el blob descifrado antes de liberarlo.
        SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
    } else {
        result = QString();
    }
    CredFree(cred);
    return result;
}

bool WinCredTokenStore::write(const QString& key, const QString& value) {
    const std::wstring target = targetName(key).toStdWString();
    QByteArray blob = value.toUtf8();

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    // Persiste para la cuenta de Windows actual en esta máquina; lo cifra DPAPI.
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    const bool ok = CredWriteW(&cred, 0);
    wipe(blob); // no dejar el secreto en claro en nuestra copia
    return ok;
}

bool WinCredTokenStore::remove(const QString& key) {
    const std::wstring target = targetName(key).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
        return true;
    // Borrar algo que no existe no es un error para nuestro contrato.
    return GetLastError() == ERROR_NOT_FOUND;
}

} // namespace pass

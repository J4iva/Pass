// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pass/google/TokenStore.h"

namespace pass {

// Implementación de TokenStore sobre el Administrador de credenciales de Windows
// (CredWriteW/CredReadW/CredDeleteW, advapi32). Cada secreto se guarda como una
// credencial genérica con TargetName "Pass/<clave>", persistencia por usuario
// (CRED_PERSIST_LOCAL_MACHINE) y cifrada por la cuenta de Windows (DPAPI interno).
// Solo se compila en WIN32.
class WinCredTokenStore : public TokenStore {
public:
    explicit WinCredTokenStore(QString prefix = QStringLiteral("Pass"));

    std::optional<QString> read(const QString& key) const override;
    bool write(const QString& key, const QString& value) override;
    bool remove(const QString& key) override;

private:
    QString targetName(const QString& key) const;
    QString m_prefix;
};

} // namespace pass

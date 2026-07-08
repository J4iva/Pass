// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace pass::sync {

// Ejecuta `git` de forma segura mediante QProcess. Decisiones de seguridad
// (modo contingencia):
//   - Programa + LISTA de argumentos, NUNCA una shell: no hay interpretación de
//     metacaracteres, así que no hay inyección de comandos.
//   - GIT_TERMINAL_PROMPT=0 y timeouts con kill: git nunca se queda colgado
//     esperando entrada por terminal.
//   - Las credenciales las gestiona Git Credential Manager (GCM): la app nunca
//     ve, pasa por argv/env ni almacena usuario/contraseña/token.
//   - isAllowedRemoteUrl(): whitelist de URLs de remoto (solo GitHub https o scp
//     sin credenciales embebidas), validada ANTES de pasar la URL a git.
//   - redacted(): saca cualquier credencial embebida de la salida antes de logs/UI.
class GitRunner : public QObject {
    Q_OBJECT

public:
    struct Result {
        bool ok = false;     // salió con código 0 y sin timeout
        int exitCode = -1;
        bool timedOut = false;
        bool started = false; // ¿se pudo lanzar el ejecutable git?
        QString stdOut;
        QString stdErr; // ya saneado con redacted()
    };
    using Callback = std::function<void(const Result&)>;

    static constexpr int kDefaultTimeoutMs = 60'000;  // 60 s
    static constexpr int kCloneTimeoutMs = 300'000;   // 5 min (clone)

    explicit GitRunner(QString repoDir, QObject* parent = nullptr);

    // Asíncrono: lanza `git <args>` en repoDir y llama a cb al terminar. Si git no
    // arranca o se agota el tiempo, cb recibe un Result con ok=false.
    void run(const QStringList& args, Callback cb, int timeoutMs = kDefaultTimeoutMs);

    // Síncrono y acotado: SOLO para el cierre de la app (push final con presupuesto).
    Result runBlocking(const QStringList& args, int timeoutMs = kDefaultTimeoutMs);

    void setRepoDir(const QString& dir) { m_repoDir = dir; }
    QString repoDir() const { return m_repoDir; }

    // Inyectables para tests (no usar en producción).
    void setGitProgram(const QString& program) { m_git = program; }
    void setAllowLocalRemotes(bool allow) { m_allowLocalRemotes = allow; }
    bool allowLocalRemotes() const { return m_allowLocalRemotes; }

    // Whitelist de URL de remoto. Acepta solo:
    //   https://github.com/<owner>/<repo>[.git][/]   (sin user:pass@)
    //   git@github.com:<owner>/<repo>[.git]
    // Rechaza file://, ssh:// arbitrario, http:// y credenciales embebidas.
    // allowLocalRemotes habilita rutas/file:// locales (solo tests).
    static bool isAllowedRemoteUrl(const QString& url, bool allowLocalRemotes = false);

    // Sustituye "esquema://usuario[:pass]@" por "esquema://***@" en cualquier texto.
    static QString redacted(const QString& text);

private:
    QString m_repoDir;
    QString m_git = QStringLiteral("git");
    bool m_allowLocalRemotes = false;
};

} // namespace pass::sync

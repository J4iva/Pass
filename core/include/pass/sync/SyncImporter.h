// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace pass::sync {

// Importa el espejo JSON del repo a la BD local. Toda la operación va en una sola
// transacción y usa SQL propio (no los repos) para preservar el updated_at remoto
// y no disparar el ciclo export<->import.
//
// Reglas (ver plan M8.2):
//  - LWW por registro: upsert solo si el updated_at remoto es más nuevo (empate => local).
//  - Tombstones explícitos: borra solo si deleted_at > updated_at local; si la fila
//    local es más nueva, sobrevive y se re-publica (anti-pérdida). Nunca "archivo
//    ausente = borrado".
//  - Validación de entrada (contingencia): solo rutas data/<entity>/<uuid>.json sin
//    path traversal, cuyo id interno coincida con el nombre; ignora > 256 KB; valida
//    tipos; campos desconocidos ignorados; manifest.format > soportado => aborta.
//  - Colisión de nombre en subjects (UNIQUE): gana el UUID lexicográficamente menor;
//    remapeo de FKs + borrado del perdedor en la misma transacción.
class SyncImporter {
public:
    struct Result {
        bool ok = false;       // ¿terminó sin error fatal?
        bool applied = false;  // ¿cambió algo en la BD local? (la UI debe refrescar)
        QString error;         // mensaje saneado si !ok
    };

    SyncImporter(QSqlDatabase db, QString repoDir);

    // Importa todo el árbol data/. Úsalo en el primer sync de un clon.
    Result importAll();
    // Importa solo las rutas indicadas (relativas al repo, separador '/'); las que
    // no sean data/** válidas se ignoran. Úsalo con el diff de un merge.
    Result importPaths(const QStringList& relPaths);

private:
    QSqlDatabase m_db;
    QString m_repoDir;
};

} // namespace pass::sync

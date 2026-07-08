// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>

namespace pass::sync {

// Vuelca el estado local de la BD al espejo JSON dentro del clon del repo:
//   <repo>/manifest.json, data/{subjects,strategies,sessions,events,tombstones}/<uuid>.json
// Escribe con QSaveFile y solo cuando el contenido difiere (diffs deterministas,
// git status limpio si no cambió nada). Borra los .json que ya no corresponden a
// una fila viva. No exporta espejos de Google (cada dispositivo los re-sincroniza)
// ni las estrategias builtin (deterministas en todos los dispositivos).
class SyncExporter {
public:
    SyncExporter(QSqlDatabase db, QString repoDir);

    // Espejo completo BD -> data/. Devuelve false ante un error de E/S o SQL.
    bool exportAll();

private:
    QSqlDatabase m_db;
    QString m_repoDir;
};

} // namespace pass::sync

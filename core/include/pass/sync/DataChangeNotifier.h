// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace pass {

// Señal global "los datos locales han cambiado". Los repositorios la emiten tras
// una escritura correcta (alta/edición/borrado) para que la sincronización entre
// dispositivos programe un push con debounce. Es un singleton para no acoplar los
// repos al servicio de sync: el core sigue sin depender de él.
//
// Falsos positivos inocuos: una notificación que no altera el espejo JSON deja
// `git status` vacío y no genera commit. El camino Google (upsert/remove por
// external_id) NO notifica: esos eventos espejo no se exportan.
class DataChangeNotifier : public QObject {
    Q_OBJECT

public:
    static DataChangeNotifier& instance();

    // Llamado por los repos tras una escritura local correcta.
    void notifyChanged();

signals:
    void changed();

private:
    explicit DataChangeNotifier(QObject* parent = nullptr);
    Q_DISABLE_COPY(DataChangeNotifier)
};

} // namespace pass

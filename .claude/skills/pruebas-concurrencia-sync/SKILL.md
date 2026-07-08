---
name: pruebas-concurrencia-sync
description: Cómo y por qué verificar el sync entre dispositivos de Pass, que corre en un hilo worker (GitSyncController + GitSyncService con 2ª conexión SQLite en WAL). Úsala al tocar GitSyncController, GitSyncService, GitRunner, la apertura de la BD (Database/WAL) o el cierre de la app (closeEvent/shutdownSync); o si aparecen avisos de afinidad de hilos de Qt, SQLITE_BUSY, cuelgues al cerrar, o congelación de la UI durante la sincronización.
---

# Pruebas de concurrencia del sync (hilo worker)

El pipeline de sincronización entre dispositivos **no corre en el hilo de la GUI**:
`GitSyncController` (hilo GUI) posee un `QThread` con un `GitSyncService` (worker).
Tocar esa zona es delicado: una concurrencia mal probada reintroduce justo los
fallos que se eliminaron. Esta skill explica **qué invariantes** deben cumplirse,
**por qué**, y **cómo** verificarlos (automático + manual).

## 1. Modelo mental (lo que NO se debe romper)

1. **La UI nunca se congela.** Toda operación lenta (red git, `merge`, import/export
   SQL) ocurre en el worker. El hilo GUI solo cachea estado y pinta.
2. **Una conexión SQLite por hilo.** El worker abre **su propia** conexión al
   *mismo fichero* (`GitSyncService::initDatabase`, en su hilo). La GUI conserva la
   suya. Nunca se comparte un `QSqlDatabase` entre hilos (Qt lo prohíbe).
3. **WAL + `busy_timeout` evitan carreras.** El fichero está en modo WAL
   (`Database.cpp`): el lector (GUI) ve siempre el último commit y no recibe
   `SQLITE_BUSY` por el escritor (worker); si ambos escriben, `busy_timeout` hace
   esperar en vez de fallar. Sin WAL, el lector se bloquearía o fallaría.
4. **Solo se entra al worker en cola.** La GUI nunca llama a un método del worker en
   directo: todo pasa por `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
   Los getters de la GUI leen **caché local**, jamás al worker ni su conexión.
5. **Cierre acotado y limpio.** `shutdownSync(budget)` hace el push final con
   presupuesto, **cierra la conexión del worker en su hilo** (`closeDatabase` vía
   `BlockingQueuedConnection`) y une el hilo. No debe haber deadlock ni fugas.

Si un cambio viola cualquiera de estos puntos, la prueba debe fallar (o el aviso
de Qt aparecerá en consola).

## 2. Pruebas automáticas

`tests/tst_gitsynccontroller.cpp` cubre el camino threaded de punta a punta:

- `pushesFromWorkerThreadAndPropagates`: escribe un dato en la **conexión GUI**,
  sincroniza por el **controller** (el worker lo exporta desde su 2ª conexión) y
  comprueba que otro dispositivo lo recibe. Valida los puntos 2 y 3 (si la 2ª
  conexión no viera el dato, o WAL fallara, el round-trip no ocurriría). Usa un
  **fichero .db real** (no `:memory:`): dos conexiones a `:memory:` serían dos bases
  distintas.
- `shutdownWithoutRepoDoesNotHang`: el cierre sin repo configurado no se bloquea
  (valida el punto 5; si hubiera deadlock, el test colgaría hasta el watchdog).

Ejecutar **toda** la batería de sync tras cualquier cambio (no solo el nuevo test):

```
ctest --test-dir build -R "gitsync|syncserializer|syncimportexport|strategycatalog|timerservice" --output-on-failure
```

Reglas al ampliar los tests:
- El controller es **asíncrono**: nunca asumas que `syncNow()` ya cambió el estado
  al volver. Corre un `QEventLoop` y espera la señal `statusChanged` a un estado
  terminal (`Idle`/`Warning`/`Error`) con un **watchdog** (60 s) que evita cuelgues.
- Para round-trips reales usa fichero, no `:memory:`.
- Repite el test "flaky-suspect" varias veces para descartar timing:
  `ctest --test-dir build -R tst_gitsynccontroller --repeat until-fail:20`.

## 3. Verificación manual (lo que un test no puede afirmar)

La **ausencia de congelación** es una propiedad de la UI; hay que verla. Tras tocar
el threading, recompila y abre la app (skill `run-pass`) y comprueba:

1. **UI fluida durante un sync lento.** Configura un repo y fuerza lentitud de red
   (p. ej. throttling, o un remoto que tarde). Mientras sincroniza: la ventana debe
   seguir respondiendo (cambiar de pestaña, mover, scroll). Antes congelaba hasta 2 min.
2. **Cierre sin cuelgue largo.** Cierra la app durante o justo tras un sync: debe
   cerrarse en ≤ el presupuesto (8 s), sin "no responde".
3. **Consola limpia.** En la salida de depuración **no** debe aparecer:
   - `QObject: Cannot create children for a parent that is in a different thread`
   - `QSqlDatabasePrivate::database: requested database does not belong to the calling thread`
   - `QObject::~QObject: Timers cannot be stopped from another thread`
   - avisos de `removeDatabase` con conexión aún en uso.
   Cualquiera de esos delata una violación de la afinidad de hilos.
4. **WAL activo.** Junto a `pass.db` deben aparecer `pass.db-wal` y `pass.db-shm`
   durante la ejecución (confirma que WAL está en uso).

## 4. Checklist antes de dar por buena la concurrencia

- [ ] Todos los tests de sync en verde, y `tst_gitsynccontroller` repetido sin fallos.
- [ ] App abierta: UI fluida durante un sync lento (verificación manual 1).
- [ ] Cierre acotado sin cuelgue (verificación manual 2).
- [ ] Consola sin avisos de hilos/SQL (verificación manual 3).
- [ ] Ningún método del worker se llama en directo desde la GUI (todo `invokeMethod`
      en cola); ningún getter de la GUI toca al worker ni su conexión.
- [ ] La conexión del worker se cierra **en su hilo** antes de pararlo.

Si algo de esto no se cumple, sigue en **modo contingencia**: no marques la tarea
como resuelta y documenta el riesgo residual.

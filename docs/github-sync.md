# Sincronización entre dispositivos (repo privado de GitHub)

Pass puede mantener tus **estadísticas** (sesiones, asignaturas, estrategias) y tus
**notas** sincronizadas entre varios equipos usando un repositorio **privado** de
GitHub como almacén. La app exporta los datos como JSON estructurado, hace `commit` y
`push` tras los cambios, y `pull`/merge al arrancar y periódicamente.

> ⚠️ **Seguridad**: Pass **nunca** ve, guarda ni transporta tu contraseña ni tokens.
> Toda la autenticación la gestiona **Git Credential Manager** (GCM), el mismo que
> usa `git` en tu sistema. Pass solo ejecuta `git` con una lista de argumentos (nunca
> a través de una shell) y solo contra URLs de GitHub validadas. Cualquier salida de
> git se sanea antes de mostrarse o registrarse.

## Requisitos

1. **Git instalado** con **Git Credential Manager** (incluido en *Git for Windows*).
   Si `git` no está en el sistema, la sección de sincronización aparece como
   *deshabilitada* y el resto de Pass funciona igual.
2. Una **cuenta de GitHub** y un **repositorio privado vacío** (créalo en
   <https://github.com/new> y marca **Private**).

## 1. Crear el repositorio

1. En GitHub, *New repository* → nombre a tu gusto (p. ej. `pass-sync`).
2. **Visibilidad: Private** (imprescindible: contiene tus datos y notas).
3. No añadas README ni .gitignore (Pass crea lo necesario en el primer push).

## 2. Conectar el primer dispositivo

1. Abre Pass → *Ajustes* → *Sincronización entre dispositivos (GitHub)*.
2. Pulsa **Clonar repositorio…**, pega la URL del repo
   (`https://github.com/usuario/pass-sync` o `git@github.com:usuario/pass-sync`) y
   elige una carpeta destino.
3. Git te pedirá iniciar sesión **en una ventana aparte** (la de GCM). Esa ventana es
   de Git/Microsoft, no de Pass; tus credenciales nunca pasan por Pass.
4. Pass prepara el repo (`manifest.json`, `.gitignore`, carpeta `notes/`), exporta tus
   datos actuales y hace el primer push.
5. Comprueba el aviso de privacidad: si Pass detecta que el repo es **público**, lo
   advierte en rojo. Hazlo privado en GitHub antes de seguir.

## 3. Conectar los demás dispositivos

En cada equipo adicional, una vez clonado el repo (con **Clonar repositorio…** o, si ya
lo clonaste a mano, con **Elegir clon existente…**), Pass importará los datos del repo
y fusionará con lo local. A partir de ahí la sincronización es automática:

- **pull/merge** al arrancar y cada 15 min,
- **push** unos 30 s después de cada cambio local (y un push final acotado al cerrar).

## 4. Notas

Pass sincroniza automáticamente **su** carpeta de notas (la subcarpeta del vault donde
Pass guarda los `.md`, p. ej. `<vault>/Pass`) con la carpeta `notes/` del repo, **viva
donde viva tu vault**. No necesitas mover tu vault ni que esté dentro del repo: cada
dispositivo puede tener su propio vault de Obsidian y aun así compartir las notas de
Pass por el repo.

- En cada sync, Pass copia tus notas de Pass al repo y trae las de los demás a tu vault.
- La primera vez la unión es **aditiva** (cero pérdida): si una nota existe en ambos
  con contenido distinto, conserva la tuya y añade la remota como `… (de otro
  dispositivo).md`.
- Solo se tocan los `.md` de **esa** subcarpeta; el resto de tu vault de Obsidian (otras
  carpetas, adjuntos, plantillas…) no se sincroniza ni se modifica.

Si prefieres que el propio repo sea tu vault de Obsidian, puedes pulsar **Usar la
carpeta notes/ del repo como vault** y abrir esa carpeta en Obsidian.

## Cómo se evitan las pérdidas y los conflictos

- **Un JSON por registro** (`data/subjects/<uuid>.json`, etc.) con `updated_at`:
  ante una edición concurrente del **mismo** registro gana la más reciente
  (*last-writer-wins*); registros distintos no chocan.
- **Borrados explícitos** mediante *tombstones* (`data/tombstones/`): un borrado solo
  se aplica si es más nuevo que tu copia local; si tu copia es más reciente,
  **sobrevive** y se vuelve a publicar (nunca se pierde por un "archivo ausente").
- **Conflictos de notas**: si el mismo `.md` cambió en dos sitios, gana el remoto pero
  **tu versión local se conserva** como copia `… (conflicto <dispositivo> <fecha>).md`.
- **Presencia**: si otro dispositivo parece tener Pass abierto, verás un aviso **no
  bloqueante** (la fusión es automática, no hay que coordinarse).

## Limitaciones

- La privacidad del repo solo se verifica de forma **heurística** (un `ls-remote` sin
  credenciales). Asegúrate tú de que el repo es privado.
- No hay bloqueo real entre dispositivos: el diseño es *merge-safe* por registro, así
  que la edición simultánea no corrompe datos, pero dos cambios al mismo registro en el
  mismo segundo se resuelven por marca temporal (uno de los dos prevalece).
- Los eventos espejo de Google Calendar **no** se sincronizan por este canal: cada
  dispositivo los obtiene de Google directamente.

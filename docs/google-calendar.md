# Sincronización con Google Calendar

Pass es open source y **no incluye credenciales de Google**: cada usuario crea su
propio "OAuth Client" gratuito en Google Cloud Console y lo pega en *Ajustes*. Así
nadie comparte secretos y la app respeta el principio de mínimo privilegio.

> ⚠️ **Seguridad**: tus tokens y el client_id/secret se guardan **solo** en el
> Administrador de credenciales de Windows (cifrados por tu cuenta de Windows).
> Nunca se escriben en ficheros, en la base de datos, en el repositorio ni en los
> logs. Pass pide el **scope mínimo** (`calendar.events`): puede ver y editar
> eventos, nada más.

## 1. Crear el proyecto y el OAuth Client

1. Entra en <https://console.cloud.google.com/> con tu cuenta de Google.
2. Crea un proyecto nuevo (arriba a la izquierda, selector de proyecto → *Nuevo
   proyecto*). Ponle el nombre que quieras, p. ej. `Pass`.
3. Habilita la API de Calendar:
   *APIs y servicios* → *Biblioteca* → busca **Google Calendar API** → *Habilitar*.
4. Configura la pantalla de consentimiento OAuth:
   *APIs y servicios* → *Pantalla de consentimiento de OAuth*.
   - Tipo de usuario: **Externo**.
   - Rellena nombre de la app, tu correo de soporte y el de contacto.
   - En **Usuarios de prueba**, añade tu propia dirección de Gmail. (Mientras la
     app esté en modo "Prueba" solo podrán entrar los usuarios de prueba; es
     justo lo que quieres para uso personal: no hace falta verificación de Google.)
   - En *Permisos* (scopes) no añadas nada extra: Pass pedirá `calendar.events`
     dinámicamente.
5. Crea las credenciales:
   *APIs y servicios* → *Credenciales* → *Crear credenciales* →
   **ID de cliente de OAuth**.
   - Tipo de aplicación: **Aplicación de escritorio**.
   - Dale un nombre y pulsa *Crear*.
6. Copia el **Client ID** y el **Client secret** que te muestra.

> ℹ️ El "secret" de un cliente de escritorio **no es realmente confidencial** por
> diseño de Google: la defensa real del flujo es **PKCE**. Aun así, Pass lo guarda
> en el Administrador de credenciales y nunca lo expone.

## 2. Conectar la cuenta en Pass

1. Abre Pass → barra lateral → **Ajustes**.
2. En *Credenciales de Google Cloud*, pega el Client ID y el Client secret y pulsa
   **Guardar credenciales**.
3. Pulsa **Conectar cuenta de Google**. Se abrirá el navegador:
   - Inicia sesión y acepta los permisos.
   - Si ves un aviso de "app no verificada", es normal en modo Prueba: entra en
     *Configuración avanzada* → *Ir a Pass (no seguro)*. Solo te lo pides a ti
     mismo porque eres un usuario de prueba.
4. El navegador mostrará "Authentication successful" y podrás cerrar la pestaña.
   Pass pasará a **Conectado** y hará la primera sincronización.

## 3. Cómo funciona

- **Bidireccional**: los eventos de Google aparecen en el calendario de Pass con
  el prefijo 🌐. Lo que crees/edites/borres en Pass sobre un evento de Google se
  aplica al instante en Google (requiere conexión).
- **Sincronización**: automática al arrancar y cada 15 min, más el botón
  *Sincronizar ahora*.
- **Eventos locales**: los que no marques para subir siguen siendo privados y
  funcionan sin conexión. Al crear un evento, marca *"Crear también en Google
  Calendar"* si quieres subirlo.
- **Desconectar**: en *Ajustes* → *Desconectar*. Pass revoca el acceso en Google y
  borra los tokens del Administrador de credenciales.

## 4. Checklist de seguridad (revisión manual de OAuth)

Para quien audite la integración:

- [ ] El flujo usa **Authorization Code + PKCE S256** (no implicit).
- [ ] El `redirect_uri` es **loopback** `http://127.0.0.1:<puerto efímero>/` y el
      servidor local solo escucha durante la autorización.
- [ ] El scope solicitado es **únicamente** `calendar.events`.
- [ ] `access_type=offline` y `prompt=consent` para obtener `refresh_token`.
- [ ] Tokens y client_id/secret **solo** en el Administrador de credenciales
      (busca en `regedit`/QSettings/ficheros: no debe haber nada).
- [ ] Ningún `qDebug`/mensaje de error imprime tokens.
- [ ] Todos los endpoints son **HTTPS** fijos (accounts.google.com /
      oauth2.googleapis.com / www.googleapis.com).
- [ ] *Desconectar* revoca el `refresh_token` y borra los tres tokens.

### Riesgo residual asumido

El Administrador de credenciales protege por **cuenta de Windows**, no frente a
malware que ya se ejecute con tu usuario. El "secret" del cliente de escritorio no
es confidencial por diseño de Google (PKCE es la defensa real).

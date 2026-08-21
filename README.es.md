<p align="center">
  <a href="https://github.com/warcrafted-server">
    <img src="https://raw.githubusercontent.com/warcrafted-server/WarCrafted-ControlP/main/Logo_github.jpg" alt="WarCrafted Universe Header" />
  </a>
</p>

<p align="center">
  🌐 <b>Idiomas / Languages:</b> <a href="README.es.md">Español 🇪🇸</a> | <a href="README.md">English 🇬🇧</a>
</p>

---

- Estado de la última compilación con azerothcore: [![Build Status](https://github.com/warcrafted-server/mod-levelsync/actions/workflows/core-build.yml/badge.svg?branch=master)](https://github.com/warcrafted-server/mod-levelsync/actions) ![WoW Version](https://img.shields.io/badge/WoW-3.3.5a-blue) ![Last Commit](https://img.shields.io/github/last-commit/warcrafted-server/mod-levelsync)


# mod-levelsync

Un módulo de AzerothCore que sincroniza personajes a través de múltiples cuentas al mismo nivel, experiencia (XP) y nivel de Progresión Individual (IP). Ofrece una experiencia de subida de nivel tradicional (1-5 jugadores) mientras mantiene a tus personajes secundarios (alts) sincronizados con tu personaje principal — sin ediciones manuales. Diseñado para servidores privados que ejecutan grandes configuraciones de bots secundarios (mod-playerbots).

---

## Características

- **Sincronización de Nivel (Level Sync)** — Los miembros de un grupo de sincronización son elevados al mismo nivel. La sincronización solo se ejecuta cuando un jugador usa el comando `.levelsync level on`. Los personajes en línea reciben actualizaciones en memoria con una notificación en el chat; los personajes fuera de línea reciben escrituras en la base de datos mediante transacciones masivas. Solo hacia arriba — la sincronización nunca reduce el nivel de un personaje.
- **Sincronización de Experiencia (XP Sync)** — La experiencia del mismo nivel se propaga como parte de la sincronización de nivel. La XP más alta en el nivel superior del grupo se envía a todos los miembros en ese nivel (en línea + fuera de línea) cuando se ejecuta el comando.
- **Sincronización de IP (IP Sync)** — Los niveles de Progresión Individual (mod-individual-progression) se alinean en todo el grupo cuando un jugador ejecuta `.levelsync IP on`. El nivel más alto del grupo se aplica a todos los que estén por debajo. Las misiones para subir de nivel ya no se propagan automáticamente — ejecuta el comando después de avanzar de nivel.
- **Grupos multicuenta** — Hasta 10 cuentas (configurable, por defecto 6) por grupo de sincronización. Cada cuenta puede tener hasta 10 personajes.
- **Solo hacia arriba** — La sincronización nunca reduce el nivel o el rango de un personaje. Si el máximo del grupo es nivel 10 / rango 5, nadie será degradado por debajo de eso.
- **Controlado por el jugador** — Nada es automático. Los miembros del grupo ejecutan `.levelsync level on` o `.levelsync IP on` cuando desean igualar el grupo. No hay un estado activado/desactivado persistente ni un enlace (hook) por evento.
- **Límite de frecuencia (Rate-limit)** — Un tiempo de recarga (cooldown) de 10 segundos por grupo evita el spam de comandos de sincronización.
- **Excepción para el Caballero de la Muerte (nivel)** — Opcional, por defecto DESACTIVADO (`0`). Cuando está en DESACTIVADO (`0`), los Caballeros de la Muerte (DK) quedan excluidos como fuentes de sincronización para objetivos por debajo del nivel 55 — evita que un DK recién creado fuerce a todos los personajes menores de nivel 55 a subir a ese nivel. Cuando está en ACTIVADO (`1`), los DK sirven de referencia normalmente.
- **Excepción para el Caballero de la Muerte (IP)** — Opcional, por defecto DESACTIVADO. La misma lógica para los niveles de IP utilizando el nivel 13 (Meseta del La Fuente del Sol / Sunwell Plateau) como umbral.
- **Sistema de clave de seguridad** — Se requieren claves con cifrado SHA-256 para vincular cuentas. Las claves persisten hasta que se sobrescriban o un GM las borre.
- **Limpieza automática de huérfanos** — En cada inicio del servidor, se purgan los datos de levelsync que hacen referencia a personajes eliminados. Los grupos vacíos se eliminan silenciosamente.
- **Pozo de Oro (Gold Pool)** — `.levelsync money` drena el oro de todos los demás miembros del grupo (en línea + fuera de línea) en la bolsa del emisor en una sola operación.
- **Desvinculación de mazmorras/bandas propia (opcional)** — `.levelsync unbindall` es el equivalente para no-GMs de `.instance unbind all`. Por defecto DESACTIVADO; pensado para servidores personales/privados donde el operador desea dar a cada jugador la comodidad de limpiar sus propios bloqueos

---

## Modelo de sincronización — lectura importante

mod-levelsync se sincroniza **solo cuando un jugador ejecuta el comando de activación**. No hay sincronización automática al iniciar sesión, cerrar sesión o recibir recompensas de misiones, ni ningún estado activado/desactivado por grupo guardado entre sesiones.

- `.levelsync level on` — aplica el nivel más alto del grupo (y la XP en ese nivel) a todos los miembros. Se ejecuta una vez y finaliza.
- `.levelsync IP on` — aplica el nivel de IP más alto del grupo a todos los miembros. Se ejecuta una vez y finaliza.

Ambos comandos respetan un tiempo de recarga de 10 segundos por grupo.

La desviación entre sesiones es **esperada** — si tu personaje principal sube 5 niveles en una sesión de juego, tus personajes secundarios no verán esos niveles hasta que alguien ejecute `.levelsync level on`. Este es un diseño deliberado: Evita niveles falsos en cascada del reflejo `SyncQuestWithPlayer` de mod-playerbots, y coloca cada cambio de estado detrás de una acción explícita del jugador.

---

## Requisitos

- AzerothCore (WotLK 3.3.5a)
- **Opcional:** [mod-individual-progression](https://github.com/azerothcore/mod-individual-progression) — requerido solo para la sincronización de IP. No es necesario para la sincronización de nivel.

---

## Instalación

1. Clona o copia este módulo dentro de tu directorio `modules/`:
   ```
   modules/mod-levelsync/
   ```

2. Recompila el servidor:
   ```bash
   cd build
   cmake ..
   make -j$(nproc)
   make install
   ```

3. Aplica la SQL a `acore_characters`:
   ```bash
   mysql -u acore -pacore acore_characters < modules/mod-levelsync/data/sql/characters/base/mod_levelsync_tables.sql
   ```

4. Copia y edita la configuración:
   ```
   azerothcore/modules/mod_levelsync.conf
   ```

5. Reinicia el worldserver.

---

## Configuración

| Opción | Por defecto | Descripción |
|--------|---------|-------------|
| `LevelSync.Enable` | `1` | Activar o desactivar el módulo por completo |
| `LevelSync.AllowLevelSync` | `1` | Permitir a los jugadores usar la sincronización de nivel |
| `LevelSync.AllowProgressionSync` | `1` | Permitir a los jugadores usar la sincronización de IP (requiere mod-individual-progression) |
| `LevelSync.AllowMoneyCommands` | `1` | Permitir a los jugadores usar `.levelsync money` para recoger el oro del grupo en la bolsa del emisor |
| `LevelSync.AllowRaidUnbind` | `0` | Permitir a los jugadores usar `.levelsync unbindall` (`.instance unbind all`para no-GMs). Desactivado por defecto; recomendado solo para servidores privados |
| `LevelSync.MaxLinkedAccounts` | `6` | Máximo de cuentas por grupo de sincronización (1–10) |
| `LevelSync.DeathKnightException` | `0` | Permitir que los DK aumenten el nivel de personajes no-DK por debajo del nivel 55 (`0` = excluido, `1` = Activado) |
| `LevelSync.DeathKnightIPException` | `0` | Permitir que los DK aumenten el nivel de personajes no-DK por debajo del nivel de IP 13 (`0` = excluido, `1` = Activado) |

---

## Tablas de la Base de Datos

Todas las tablas se añaden a `acore_characters`. No se modifica ninguna tabla del world (core).

| Table | Purpose |
|-------|---------|
| `levelsync_groups` | Una fila por grupo de sincronización. Las columnas `level_sync_enabled` y `sync_progression` se conservan de versiones anteriores, pero la versión actual ya no las lee — el estado de sincronización se gestiona completamente mediante los comandos y la configuración del servidor. |
| `levelsync_members` | Una fila por personaje en un grupo. Vincula `char_guid` y `account_id` a un `group_id`. |
| `levelsync_account_keys` | Almacena la clave de seguridad con cifrado SHA-256 por cuenta. Necesaria para vincular cuentas. Máximo una fila por cuenta. |

Los datos de los niveles de IP se almacenan en la tabla existente `character_queststatus_rewarded` utilizando IDs de misiones ocultos 66001–66018 (el formato de mod-individual-progression). mod-levelsync no añade ninguna tabla específica para IP.

---

## Cómo funciona la sincronización

### Añadir personajes

Utiliza `.levelsync addaccount <account>` o `.levelsync addchar <name>` para añadir un nuevo miembro. Los personajes recién añadidos conservan su nivel y rango existentes hasta que alguien ejecuta el comando de activación — así que un personaje secundario de nivel bajo no subirá al límite del grupo hasta que el grupo esté listo.

### Activadores (Triggers)

| Evento | Sincronización de nivel | Sincronización de IP |
|-------|-----------|---------|
| El jugador inicia sesión | Sin efecto. | Sin efecto. |
| El jugador cierra sesión | Sin efecto. | Sin efecto. |
| El jugador sube de nivel | Sin efecto. La desviación se reconcilia en la siguiente ejecución de `.levelsync level on`. | N/A |
| El nivel de IP avanza (recompensa de misión o`.ip set`) | N/A | Sin efecto. La desviación se reconcilia en la siguiente ejecución de `.levelsync IP on`. |
| `.levelsync level on` | Sincronización completa: todos los miembros se elevan al nivel más alto, con envío de XP en cada límite efectivo (gestiona los límites de DK / no-DK de forma independiente). | — |
| `.levelsync IP on` | — | Sincronización completa: todos los miembros se elevan al nivel más alto. |

Todas las rutas anteriores de sincronización automática (`OnPlayerLogin`, `OnPlayerLogout`, `OnPlayerCompleteQuest`) no realizan ninguna operación en la versión actual. Los métodos internos del gestor (`SyncGroupOnLogin`, `SyncIPOnLogin`, `SyncIPOnTierUp`, etc.) siguen presentes en el código base para que el comportamiento antiguo se pueda restaurar desmarcando los comentarios en los cuerpos de los ganchos (hooks) en `LevelSyncPlayerScript`.

### Excepción del Caballero de la Muerte

Cuando `LevelSync.DeathKnightException = 0` (por defecto), un DK queda excluido como fuente de sincronización para personajes por debajo del nivel 55 — no aplicará su nivel 55 a los miembros del grupo menores de nivel 55. El DK aún puede ser subido de nivel por personajes que no sean DK. Una vez que todos los personajes no-DK del grupo alcancen el nivel 55+, el DK participa normalmente como fuente de sincronización. Cuando se establece en `1`, los DK participan inmediatamente y pueden elevar el nivel de los personajes menores a 55. La misma lógica se aplica a `LevelSync.DeathKnightIPException` utilizando el nivel 13 como umbral.

---

## Comandos de jugador

Todos los comandos comienzan con `.levelsync`.

### Configuración inicial

| Comando | Descripción |
|---------|-------------|
| `.levelsync setkey <key>` | Establece una clave de seguridad para tu cuenta. Otros jugadores necesitan esta clave para vincular tu cuenta a su grupo. Las claves persisten hasta que se sobrescriban. |
| `.levelsync addaccount <account> [key]` | Vincula todos los personajes de otra cuenta a tu grupo de sincronización. |
| `.levelsync addchar <charname> [key]` | Vincula un solo personaje a tu grupo de sincronización. La clave es necesaria si el personaje pertenece a una cuenta diferente |
| `.levelsync removeaccount <account>` | Elimina todos los personajes de una cuenta de tu grupo de sincronización por el nombre de la cuenta. |
| `.levelsync removeaccount # <accountid>` | Elimina todos los personajes de una cuenta mediante el ID numérico de la cuenta (p. ej., `# 105`). |
| `.levelsync removechar <charname>` | Elimina un solo personaje de tu grupo de sincronización. |
| `.levelsync removeall` | Disuelve todo tu grupo de sincronización. |
| `.levelsync disbandaccount` | Disuelve cada grupo de sincronización asociado con cualquier personaje de tu cuenta. Funciona incluso si el personaje con el que has iniciado sesión no está personalmente en un grupo. |
| `.levelsync listaccount <account> [key]` | Muestra todos los personajes de una cuenta con su nivel, clase y estado de grupo. La clave es necesaria al consultar otra cuenta — no es necesaria para la tuya. |

### Estado y Activaciones

| Comando | Descripción |
|---------|-------------|
| `.levelsync status` | Muestra el resumen de tu grupo de sincronización: ID de grupo, cantidad de cuentas, estado de permisos de configuración para la sincronización de nivel e IP, y todos los miembros con nivel en vivo, clase y nivel de IP. Finaliza con un enlace al addon LevelsyncUI. |
| `.levelsync level on` | Ejecuta una única resincronización completa de nivel + XP (se aplican reglas de DK con límites múltiples). Vuelve a estar en estado `Available` después. Sujeto a un tiempo de recarga de 10 segundos por grupo. `.levelsync level off` no realiza ninguna operación — no hay un estado persistente que desactivar. |
| `.levelsync IP on` | Ejecuta una única resincronización completa del nivel de IP. Vuelve a estar en estado `Available` después. El mismo tiempo de recarga de 10 segundos. `.levelsync IP off` no realiza ninguna operación. |
| `.levelsync money` | Operación de un solo uso. Drena el oro de todos los demás miembros del grupo (en línea + fuera de línea) a tu bolsa. Se rechaza si la bolsa resultante superaría el límite de oro (`MAX_MONEY_AMOUNT`) — retira fondos manualmente primero si es así. Se rechaza si el grupo solo te incluye a ti o si nadie tiene oro. Los miembros en línea drenados reciben una notificación en el chat. Sujeto al mismo tiempo de recarga de 10 segundos que los activadores. |
| `.levelsync unbindall [name]` | Operación de un solo uso. Borra cada registro de estancia que el objetivo tenga en las 4 ranuras de dificultad (mazmorras + bandas), conservando el registro del mapa en el que se encuentra actualmente el objetivo. **Sin argumento** → jugador seleccionado por clic/tabulador, o tu mismo como opción predeterminada (misma semántica que el `.instance unbind all`estándar). **Con nombre** → jugador en línea que coincida con ese nombre (no distingue mayúsculas de minúsculas); se rechaza si está fuera de línea. Requiere `LevelSync.AllowRaidUnbind = 1` en el servidor; de lo contrario, se rechaza con un mensaje de "desactivado por el servidor". Sin tiempo de recarga. |

### Mensaje de rechazo por tiempo de recarga

Si ejecutas la sincronización demasiado rápido, verás:

```
[LevelSync] Must wait N second(s) before resync.
```

### Ejemplo de salida de estado

En el juego, la salida aparece con códigos de color: `[LevelSync]` en verde, `Available` en verde, `Disabled` en rojo, los nombres de los personajes con el color de su clase, los nombres de la clase en dorado y las etiquetas de nivel de IP en colores específicos del nivel[cite: 2]. El estado de nivel/progresión refleja lo que permite la configuración del servidor (si `.levelsync level on` / `.levelsync IP on` se ejecutarán cuando se invoquen), no un interruptor persistente[cite: 2]. Se muestra aquí en texto plano:

```
[LevelSync] Sync Group #1
  Accounts: 3/6
  Total Characters: 9
  Level sync: Available
  Progression sync: Available
[LevelSync] Group members:
  Account 105: Characters: 3
    Aone (lvl 60) (Druid) IP Tier: 7 - Naxxramas 40
    Atwo (lvl 60) (Paladin) IP Tier: 7 - Naxxramas 40
    Athree (lvl 60) (Death Knight) IP Tier: 13 - Sunwell Plateau
  Account 106: Characters: 3
    Bone (lvl 60) (Hunter) IP Tier: 7 - Naxxramas 40
    ...
[LevelSync] For a graphical interface use the addon: https://github.com/Lichborne-AC/LevelsyncUI
```

---

## Comandos de GM

| Comando | Descripción |
|---------|-------------|
| `.levelsync gm removeall <charname>` | Disuelve por completo el grupo de sincronización al que pertenece el personaje nombrado y elimina todas las claves de cuenta asociadas. Si el personaje no está en un grupo, elimina solo su clave de cuenta. |
| `.levelsync gm xp <amount>` | Concede XP a tu objetivo actual (o a ti mismo si no hay objetivo). Utiliza `Player::GiveXP` para pasar por la secuencia normal de subida de nivel de AC — incluyendo el multiplicador de tasa de XP de mod-playerbots cuando se aplica a un bot. Útil para probar las rutas de propagación de XP. |
| `.levelsync gm unbindall [name]` | Contraparte restringida a GMs de `.levelsync unbindall`. Misma resolución de objetivo: jugador seleccionado por clic/tabulador con opción predeterminada a ti mismo si no hay argumento, o jugador en línea por nombre (no distingue mayúsculas de minúsculas). Siempre disponible para GMs independientemente de `LevelSync.AllowRaidUnbind` — ese indicador solo controla la versión para jugadores[cite: 2]. Solo para objetivos en línea. |

### Trabajar con `.ip set`

`.ip set` lo proporciona mod-individual-progression, no mod-levelsync. Es la forma recomendada de hacer avanzar a un personaje a un nivel de IP específico. Después de usarlo en un miembro, ejecuta `.levelsync IP on` para aplicar el nuevo nivel al resto del grupo.

```
.ip set <player> <tier>     # e.g. .ip set Aone 5
```

---

## Addons de interfaz

- [PlayerbotManager](https://github.com/Lichborne-AC/PlayerbotManager) — Addon complementario para mod-playerbots. Útil junto con mod-levelsync para gestionar la lista de bots secundarios sobre la que se construye un grupo de sincronización.
- [LevelsyncUI](https://github.com/Lichborne-AC/LevelsyncUI) — Un addon de World of Warcraft (WotLK 3.3.5a, AzerothCore) que proporciona una interfaz gráfica para mod-levelsync. Recomendado pero no obligatorio — toda la funcionalidad está disponible mediante comandos con punto sin el addon. El enlace también se imprime al final de cada salida de `.levelsync status`.

---

## Licencia

GPL v2

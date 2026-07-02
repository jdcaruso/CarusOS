# Revisión de código — CarusOS (Fable 5)

- **Fecha:** 2026-07-02, sobre `dev` @ `53c230b` (v0.40.0)
- **Generado por:** Claude Fable 5 (revisión de código completa del sketch)
- **Para:** Opus 4.8 — fixear los ítems marcados `TODO`, en orden de prioridad.
- **Regla del repo:** cada fix bumpea el PATCH de `CARUSOS_VERSION` + badge del
  README + `CARUSOS_SOURCE_DATE` (ver CLAUDE.md). Un fix por vez: el usuario
  compila y prueba en hardware entre cambios. No apilar refactors sin probar.

## Estado del batch (v0.40.2, por Opus 4.8)

**Todos los ítems 2–11 fueron implementados en `dev` como un solo batch
(v0.40.2), sin compilar todavía.** El usuario compila y prueba en hardware.
Los estados `TODO` de abajo quedan como referencia histórica; lo aplicado:

- **Ítem 2** (galería): estado por-instancia en `malloc` + cleanup en
  `LV_EVENT_DELETE` + handoff `orphaned` al task de descarga. Sin UAF.
- **Ítem 3** (FS fuera del core UI): `BCMD_FS_LIST` / `BCMD_FS_FORMAT` en la
  cola de Core 0; la UI pide y sondea con `backend_fs_poll()`. ✅ **Resuelto:
  el boot loop del primer intento era basura de flasheos viejos en la región
  FAT (`esptool erase_region 0x610000 0x9E0000` lo arregló). v0.43.0 probado
  OK en hardware con FS + Explorer habilitados.** Ver detalle al final del
  ítem 3.
- **Ítem 4** (XCA9554): mutex (`expander_mutex`) alrededor de los dos setters.
- **Ítem 5** (framebuffer): flag `fb_dirty`; solo se empuja el buffer al panel
  cuando LVGL dibujó algo.
- **Ítem 6** (CI): `.github/workflows/build.yml` con `arduino-cli` (esp32
  3.3.10, FQBN del README, `secrets.h` dummy). *Verificar libs/`lv_conf.h` en
  el primer run.*
- **Ítem 7** (strings): migrados a `TXT_*` (EN+ES) en `language.h`.
- **Ítem 8** (IDs): `enum carusos_app_id_t` (APP_CALC…APP_IMU).
- **Ítem 9** (código muerto): borrados `backend_get_time()`, `my_print()` y los
  3 macros de HTTP OTA fantasma.
- **Ítem 10** (pines): centralizados en `pin_config.h` (`PIN_I2C_*`, `EXP_PIN_*`).
- **Ítem 11** (PSRAM): `MALLOC_CAP_SPIRAM` con fallback a interna.

---

## Contexto verificado (importante, leer antes de fixear)

El core arduino-esp32 instalado es **3.3.10** y su HAL de I2C
(`esp32-hal-i2c.c`) tiene un **mutex por bus**: cada `i2cWrite`/`i2cRead`
toma `bus[num].lock`. Wire, SensorLib (GT911/RTC/IMU), el expansor XCA9554
de Arduino_GFX y el es8311 (usa `i2cWrite` del HAL directo) pasan todos por
ese lock. **Conclusión: las transacciones I2C individuales entre cores son
atómicas — NO hace falta un mutex global de I2C.** Solo quedan carreras
lógicas multi-transacción (ver ítem 4).

---

## Ítem 1 — `telnetClient` compartido entre cores sin serializar

**Estado: FIXED (v0.40.1, por Fable 5 en esta sesión — verificar en hardware)**

- **Dónde:** `backend_core.cpp` — `backend_log()` + loop de `Task_Backend`.
- **Problema:** `backend_log()` está registrado como callback de log de LVGL
  (`CarusOS_ESP32S3.ino:23`) y por lo tanto corre en **Core 1**, escribiendo
  en `telnetClient`. En paralelo, el loop de Core 0 hace `telnetClient.stop()`
  y reasigna el cliente al aceptar conexiones. Use-after-free del socket si
  un log sale mientras Core 0 reemplaza el cliente. (Probable causa raíz del
  crash de telnet parcheado en v0.39.)
- **Fix aplicado:** `backend_log()` ya no toca el socket: encola el mensaje en
  una cola FreeRTOS (`log_queue`, 16 × 160 bytes, drop si está llena) y
  `Task_Backend` (Core 0) la drena y escribe al cliente. Core 0 es ahora el
  único dueño del socket.

## Ítem 2 — Galería: use-after-free al tocar "Atrás" durante la descarga

**Estado: TODO — prioridad alta (latente: la app está deshabilitada hoy)**

- **Dónde:** `ui_core.cpp`, `app_build_gallery()` (~línea 462).
- **Problema:** el `lv_timer` que espera el download no está atado a la vida
  de la pantalla. Si el usuario vuelve atrás antes de que `gal_state.finished`
  se ponga en true, la pantalla y `gal_state.label`/`img` se destruyen, pero
  el timer sigue vivo y al completar hace `lv_label_set_text()` sobre un
  objeto liberado. Además, reabrir la app rápido crea un segundo task/timer
  sobre el mismo `gal_state` estático.
- **Fix sugerido:** replicar el patrón de la app Animation: estado en
  `lv_malloc`, cleanup colgado de `LV_EVENT_DELETE` del `app_screen` que
  borra el timer (y marca el download como huérfano para que el task libere
  el buffer). Hoy `CARUSOS_APP_GALLERY = 0`, así que no muerde — pero hay que
  arreglarlo ANTES de rehabilitar la app.

## Ítem 3 — El "backend" de archivos corre en el core de UI

**Estado: TODO — prioridad alta (posible causa del crash del Explorer en debug)**

- **Dónde:** `ui_core.cpp` `fs_format_event_cb()` / `app_build_explorer()`
  llaman sincrónicamente a `backend_list_files()` y `backend_format_fs()`
  (`backend_core.cpp`).
- **Problema:** FFat corre en **Core 1** (el callback de LVGL), no en Core 0.
  Contradice la arquitectura, congela el render durante el format, y las
  operaciones FS pesadas corren en el stack del task de GUI. El propio código
  tiene instrumentación de debug de un crash sin resolver (v0.40 crash hunt).
- **Fix sugerido:** agregar comandos `BCMD_FS_LIST` / `BCMD_FS_FORMAT` a la
  cola UI→backend existente; el resultado se deja en un buffer + flag
  `volatile` que la UI recoge con un `lv_timer` (mismo patrón que la galería).
  Nota: `CARUSOS_USE_FS = 0` hoy, mismo criterio que el ítem 2.

### ⚠️ Hallazgo hardware (v0.43.0, Opus 4.8 — FALLÓ, rollback aplicado)

Se implementó el fix (list/format ahora en la cola de Core 0 —
`backend_fs_request_list/format()` + `backend_fs_poll()`; la UI sondea con un
`lv_timer` atado a `LV_EVENT_DELETE`) y se probó en hardware prendiendo
`CARUSOS_USE_FS=1` + `CARUSOS_APP_EXPLORER=1` (intento v0.43.0).

**Resultado: boot loop infinito.** El crash **NO** está en el path de UI que
arregla este ítem, sino en el **mount de FFat en el arranque**
(`FFat.begin(false)` dentro de `Task_Backend`, Core 0, `backend_core.cpp` ~L378).
Log serial (se repite en loop):

```
CarusOS Initialized.
[NVS] loaded: wifi=1 telnet=1 anim=0 bt=0
[Backend] Starting WiFi Connection...
[Backend] Checking for FAT partition...
[Backend] Mounting FFat...
        <-- reset justo acá
rst:0xc (RTC_SW_CPU_RST),boot:0x2b (SPI_FAST_FLASH_BOOT)
Saved PC:0x4037e211
```

Muere en `[Backend] Mounting FFat...`, **antes** de imprimir "FFat Mounted" o
"FFat not mounted". `rst:0xc` = reset por software (panic/abort o watchdog), no
brownout. El "Checking for FAT partition" pasó sin el mensaje CRITICAL → o sea
`esp_partition_find_first` SÍ encontró la partición FAT; el abort es dentro de
`FFat.begin(false)` sobre esa partición.

**Estado:** rollback a `CARUSOS_USE_FS=0` / `CARUSOS_APP_EXPLORER=0` (vuelta a
v0.42.0, con Gallery OK). El fix de sacar FS del core de UI queda en el árbol
(compilado-fuera), listo para cuando se resuelva el mount.

### ✅ RESUELTO (2026-07-02, Fable 5 + prueba en hardware)

**Causa raíz: basura de flasheos anteriores en la región FAT de la flash.**
El primer mount de la partición hace que la capa de wear-leveling (`wl_mount`)
lea su config en los últimos sectores de la partición; con datos residuales
"convincentes" toma offsets inválidos y aborta en vez de fallar limpio (por eso
`begin(false)` crasheaba en lugar de devolver `false`). Fix aplicado una sola
vez en el device:

```
esptool erase_region 0x610000 0x9E0000
```

Tras el erase, v0.43.0 bootea y el FS funciona. `CARUSOS_USE_FS=1` y
`CARUSOS_APP_EXPLORER=1` quedan habilitados; la nota del workaround quedó en
el comentario del flag en `carusos_config.h`.

**Hipótesis descartadas durante el diagnóstico (verificado contra el core
3.3.10 instalado):**
- Label de partición: el CSV de `app3M_fat9M_16MB` usa exactamente `ffat`,
  el default de `FFat.begin()`. Y el wrapper devuelve `false` en todos sus
  paths de error — el abort venía de `wl_mount`.
- Conflicto flash-write vs panel RGB: los flags `LCD_RGB_ISR_IRAM_SAFE` /
  `GDMA_ISR_IRAM_SAFE` no están en el sdkconfig del core (riesgo teórico
  real), pero el erase solo bastó → no era esto. Tener presente igual para
  escrituras FS/OTA intensas con el panel andando (glitches posibles; la
  solución de fondo es sdkconfig custom vía PlatformIO, ya en el roadmap).

**TODO restante (heredado, sigue valiendo):** el mount de boot no degrada
con gracia — si `wl_mount` aborta, se lleva el arranque entero a boot loop.
Dato útil para diagnósticos futuros: el core tiene
`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` y el scheme incluye partición
`coredump` en `0xFF0000` — un crash deja el dump grabado y se extrae con
`esptool read_flash 0xFF0000 0x10000 core.bin` + `espcoredump.py` con el ELF
del build.

## Ítem 4 — Carrera lógica en el cache del expansor XCA9554

**Estado: TODO — prioridad media (ventana chica, consecuencia visible)**

- **Dónde:** `lvgl_port.cpp` — `lvgl_port_set_backlight()` (llamada desde el
  touch en Core 1) y `lvgl_port_set_audio_amp()` (llamada desde `Task_Audio`
  en Core 0).
- **Problema:** el driver XCA9554 de Arduino_GFX cachea el registro de salida
  en RAM y hace read-modify-write de ese cache en `digitalWrite()`. El lock
  del HAL protege la transacción I2C pero NO el cache: si ambos cores
  escriben pines a la vez, uno puede pisar el estado del otro (ej. apagar el
  amplificador al togglear el backlight).
- **Fix sugerido:** un mutex chico en `lvgl_port.cpp` alrededor del cuerpo de
  ambos setters (son los únicos dos puntos que escriben el expansor en
  runtime).

## Ítem 5 — Framebuffer completo empujado cada 5 ms

**Estado: TODO — prioridad media (performance/energía, no crash)**

- **Dónde:** `lvgl_port.cpp` `lvgl_port_flush()` + `Task_GUI` en el `.ino`.
- **Problema:** con `DIRECT_RENDER_MODE` (y `RGB_PANEL` comentado),
  `lvgl_port_flush()` hace `gfx->draw16bitRGBBitmap(0, 0, buf, 480, 480)`
  (~460 KB desde PSRAM) en cada iteración del loop de GUI, aunque no haya
  cambiado ni un pixel.
- **Fix sugerido:** en `my_disp_flush()` setear un flag `static volatile bool
  fb_dirty = true;` y en `lvgl_port_flush()` copiar solo si estaba seteado
  (y limpiarlo). Con pantalla estática el ahorro de CPU/ancho de banda es
  enorme.

## Ítem 6 — CI de compilación con arduino-cli

**Estado: TODO — prioridad media (mejor retorno/esfuerzo de la lista)**

- arduino-cli está instalado localmente (`C:\Program Files\Arduino CLI`).
  Agregar un GitHub Action que en cada push haga `arduino-cli compile` con el
  FQBN correcto (ESP32S3, flash 16MB, partición 3MB APP/9.9MB FATFS, OPI
  PSRAM, QIO 80MHz — ver README), generando un `secrets.h` dummy desde
  `secrets.h.example`. El repo es público: cuida el build sin exponer nada.

## Ítem 7 — Strings visibles fuera de `language.h` (viola la convención propia)

**Estado: TODO — prioridad baja**

- **Dónde:** `ui_core.cpp`: "Fecha y Hora" (en `g_apps[]`), "Archivos",
  "Opciones", "Galeria", "Sys Info", "Update", "Formateando...", los textos
  del Mic Test, del IMU ("Movimiento (IMU)", "IMU no detectado"), "Sistema",
  "Hardware", "System Update", y varios más.
- **Problema:** con `CARUSOS_LANG_ES 0` queda spanglish. CLAUDE.md exige que
  todo string visible pase por `TXT_*` con EN y ES sincronizados.
- **Fix sugerido:** migrar a macros `TXT_*` nuevos en `language.h` (EN + ES).

## Ítem 8 — IDs mágicos de apps

**Estado: TODO — prioridad baja**

- `add_app_icon(app_grid, 5)`, `app->id == 4`, etc. en `ui_core.cpp`.
- **Fix sugerido:** `typedef enum { APP_CALC = 0, APP_DATETIME, ... }` y usar
  los nombres en `g_apps[]`, menús y el launcher.

## Ítem 9 — Código muerto / config sin uso

**Estado: TODO — prioridad baja**

- `backend_get_time()` (`backend_core.cpp:449`): superada por
  `backend_get_datetime()`; además exige WiFi cuando el RTC ya da hora. Borrar
  y migrar llamadas si quedara alguna.
- `my_print()` en `lvgl_port.cpp`: registrada y luego pisada por `my_log_cb`
  del `.ino` (última registración gana). Borrar una de las dos vías.
- `OTA_UPDATE_URL`, `OTA_CHECK_INTERVAL_MIN`, `CARUSOS_USE_HTTP_OTA` en
  `carusos_config.h`: nadie los usa (el HTTP OTA nunca se implementó). Borrar
  o implementar; no dejar config fantasma.

## Ítem 10 — Pines dispersos y duplicados

**Estado: TODO — prioridad baja**

- Los pines I2C (47/48) están hardcodeados en `lvgl_port.cpp`,
  `rtc_pcf85063.cpp` e `imu_qmi8658.cpp`; los pines del expansor (3=amp,
  5=backlight, 6=reset panel) son números mágicos en `lvgl_port.cpp`.
- **Fix sugerido:** centralizar todo en `pin_config.h` (`PIN_I2C_SDA`,
  `PIN_I2C_SCL`, `EXP_PIN_AMP`, `EXP_PIN_BACKLIGHT`, ...).

## Ítem 11 — Alloc "PSRAM" que no garantiza PSRAM

**Estado: TODO — prioridad baja**

- `backend_core.cpp` `backend_download_file()`: usa
  `heap_caps_malloc(len, MALLOC_CAP_8BIT)` con un comentario que dice PSRAM,
  pero ese flag puede asignar heap interna (y comérsela con un archivo
  grande). Usar `MALLOC_CAP_SPIRAM`.

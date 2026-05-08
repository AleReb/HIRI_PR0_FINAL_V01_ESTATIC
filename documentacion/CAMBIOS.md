# Registro de Cambios - FirmwarePro

## [Pro V0.1.15V] - 2026-04-30
### Sincronización de documentación con firmware actual

- Se actualizó la documentación para reflejar el sketch activo `HIRI_PR0_FINAL_V01_ESTATIC.ino`.
- Se actualizó la versión documentada a **Pro V0.1.15V**.
- Se documentó `DEVICE_ID_STR = "10"` y el AP WiFi `HIRIPRO_10`.
- Se documentó la configuración activa HIRIDUST/VALPO:
  - BTN1 en GPIO39.
  - BTN2 deshabilitado por defecto (`BUTTON_PIN_2 = -1`).
  - PMS5003 en RX18/TX5.
  - SDS198 por `Serial2` en RX23/TX19.
  - SD por HSPI en SCLK14/MISO2/MOSI15/CS13.
- Se actualizó la lista de sensores documentados:
  - PMS5003.
  - SDS198.
  - SHT4x/SHT31.
  - DFRobot MultiGas I2C.
  - ENS160.
  - RTC DS3231.
- Se documentaron los defaults operativos actuales:
  - Guardado SD cada 30 segundos.
  - Envío HTTP cada 5 minutos.
  - Autostart activado.
  - Auto debug activado.
  - GNSS desactivado por defecto.
- Se actualizó el formato CSV con columnas de SHT4x, gas, TVOC, eCO2, AQI y `notas`.
- Se documentaron archivos auxiliares:
  - `/errors_h<ID>.csv`
  - `/failed_h<ID>.csv`
- Se corrigió el valor de datos HTTP faltantes o inválidos: actualmente se envían como `-1`.
- Se actualizó el mapeo HTTP para dispositivo 10.
- Se corrigieron comandos de compilación/carga Arduino CLI para usar la carpeta actual:
  - `arduino-cli compile --fqbn esp32:esp32:esp32 .`
  - `arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 .`

Impacto:

- Cambio documental. No modifica firmware.
- Reduce riesgo de operar con instrucciones antiguas de pines, versión, CSV o comandos de build.

## [V0.1.0] - 2026-02-14
### Primera versión formal (baseline de producto)

- Se establece versión de firmware en código a **Pro V0.1.0**.
- Ajustes finales de UI WiFi:
  - Pantalla WiFi dedicada mostrando SSID, PASS e IP.
  - Control por botones según estado WiFi.
  - Con WiFi activo, pantalla bloqueada para evitar salida accidental.
  - Toggle ON/OFF por BTN2.
- Se repone feedback visual de arranque de módem:
  - Parpadeo de LED RGB durante `MODEM Starting...`.
- Documentación ordenada en carpeta `documentacion/` con `README.md` en raíz y `LICENSE` en root del repo.

## [V0.0.35] - 2026-02-14
### WiFi SD, documentación y control de release

- Se consolidó versión de firmware a **Pro V0.0.35**.
- Se implementó flujo de **WiFi SD** con mejoras para uso desde Windows mediante captive portal DNS.
- Se creó documentación formal en Markdown:
  - `README.md`
  - `MANUAL_USUARIO.md`
  - `MANUAL_TECNICO.md`
  - `LICENSE` (CC BY-NC 4.0)
- Se realizó recuperación/validación de estabilidad tras pruebas.
- Commit con funcionamiento confirmado en equipo:
  - `6c66dbb`
  - Flasheado exitosamente en COM5 con verificación hash y reset OK.

## [V0.0.33] - 2026-02-14
### Notas operativas en CSV + menú Mensajes

- Se agregó nueva columna `notas` al CSV.
- Se agregó opción **Otros** al submenú **Mensajes**.
- Lógica de nota one-shot:
  - Al seleccionar `Camion`, `Humo`, `Construccion` u `Otros`, se guarda esa nota en la próxima fila CSV.
  - Luego se limpia automáticamente para evitar repetición.

## [V0.0.26] - 2026-02-13
### Mejoras en UI y lógica de muestreo

- Se reemplazó el aviso simple por un prompt de confirmación para iniciar/detener muestreo.
- BTN1 cancela la acción y vuelve al menú.
- BTN2 confirma e inicia/detiene el proceso.
- Al confirmar inicio, se fuerza una ejecución inmediata de guardado SD y envío HTTP.
- Se añadieron mensajes por puerto serial para rastrear clics de botones.

## [V0.0.25] - 2026-02-13
### Reestructuración de navegación

- Se eliminó el acceso por pulsación larga.
- Se agregó modo FULL como opción del menú principal.
- La salida de modo FULL se realiza con BTN1 en esa versión.
- Se deshabilitó la función hold para evitar saltos accidentales de pantalla.

## [V0.0.24] - 2026-02-13
### Seguridad de operación

- Se implementó Full Lock para bloquear navegación mientras el dispositivo está en modo de visualización de datos.
- Se sincronizó BTN2 para alternar inicio/parada tanto en menú como en vista bloqueada.

## [V0.0.23] - 2026-02-13
### Versión inicial fusionada

- Integración de backend GPSDebug con UI de menús HIRI_PR0.
- Soporte para sensores PMS5003, SHT31 y SDS198.
- Registro en SD y transmisión HTTP concurrente.

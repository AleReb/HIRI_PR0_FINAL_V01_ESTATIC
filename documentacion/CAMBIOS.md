# Registro de Cambios - FirmwarePro

## [Documentación] - 2026-10-26

Revisión integral contra el código de trabajo **Pro V0.1.29R**; no modifica el
firmware ni incrementa su versión.

- README, manual técnico, manual de usuario, estructura de menú y guía de
  desarrollo sincronizados; se integraron las secciones añadidas y se quitaron
  afirmaciones operativas que el flujo actual no implementa.
- Estado actual documentado: ID **3**, AP `HIRIPRO_3`, APN `gigsky-02`.
  Estos valores pertenecen a la copia revisada; los IDs de las versiones
  históricas siguientes describen su contexto de entonces.
- Conexión: SIM lista, registro celular y datos antes de HTTP; tres intentos
  totales, 5 s entre intentos y pausa de una hora tras agotar la conexión.
- Se aclara que la reconexión ocurre al siguiente turno HTTP, la pausa se pierde
  al reiniciar y los errores del servidor no activan esa pausa.
- Registro SD cada 180 s durante esperas de conexión; primera muestra al vencer
  el intervalo. No se promete registro durante WiFi SD ni después de `stop`.
- Enable I²C GPIO0 activo HIGH, corte 250 ms y estabilización 700 ms;
  sin dependencia de SIM/red. Se documenta discrepancia RX/TX del constructor PMS.
- CSV corregido a las 27 columnas reales, sin SHT31; valores ausentes y fecha
  de respaldo distintos de la política HTTP. Un archivo no vacío conserva su cabecera.
- Telemetría 1–13 de 17 valores y formato HTTPS del 14 de siete valores;
  para equipos posteriores se exigen mapeos reales.
- Se corrige la recomendación de `start` serial: deja logging apagado al iniciar;
  se documenta BTN1 debug como envío/guardado puntual y BTN2 deshabilitado.
- Se documenta que el reinicio por horas se almacena pero no se ejecuta en loop;
  tampoco se programan `syncRtcSmart()` ni `downloadXtraIfDue()` automáticamente.
- Validación de esta revisión: contraste de fuentes y comprobación de enlaces,
  cabecera, identidad, APN y mapeos; no se cargó firmware ni se enviaron datos
  al backend. Las pruebas/compilaciones previas se mantienen como evidencia
  separada y no sustituyen validación física.
- Incidente reportado: posible problema de energía; causa sin confirmar mediante
  un registro de reset o medición incluido en el repositorio.

Las entradas de versiones anteriores se conservan como historial, no como
configuración vigente. Consultar README y manuales para el estado actual.

## [Pro V0.1.29R] - 2026-10-26

- Nuevo formato estatico desde equipo 14: HTTPS y 7 valores Plantower/SHT41.
- Mapeo confirmado del equipo 14: 1232,1232,1232,1232,1232,1233,1233;
  variables 3,6,7,8,9,3,6.
- Equipos 1 a 13 conservan URL y formato anterior; ID activo conservado en 13.
- Equipos posteriores requieren sus IDs reales; se rechaza envio sin mapeo.
- SHT41 ausente no se sustituye por SHT31 en el nuevo formato.
- Se conservan el registro SD y la politica de 3 intentos sin SIM/red.
- Pruebas aprobadas: URL exacta/orden del equipo 14, compatibilidad 1-13,
  datos ausentes, equipos sin mapeo y regresion de registro sin red.
- Compilacion ESP32 aprobada: flash 1.050.357 bytes (80%), RAM 49.968 bytes (15%).
- Impacto: cambia el payload del equipo 14; validacion en modem/backend pendiente.
- Commit relacionado: pendiente.

## [Pro V0.1.28R] - 2026-10-26

- Registro local habilitado antes del modem y conservado sin SIM o sin red.
- Hasta 3 intentos de conexion celular por ciclo, separados por 5 s, con pausa
  de 1 hora al fallar; comprobacion de SIM lista y registro LTE/paquetes.
- Reemplazo de gprsConnect bloqueante por comandos AT con timeout y atencion
  de sensores/SD/watchdog durante la espera, incluido HTTP y XTRA.
- Hasta 3 pruebas AT al arrancar; no iniciar GNSS si el modem no responde.
- Se conservan las modificaciones locales de identificacion/backend del usuario.
- Impacto: la red deja de detener el registro local; requiere SD operativa.
- Pruebas simuladas: 7 escenarios de conexion y esperas AT con guardado SD,
  incluida recuperacion de red y desbordamiento de millis(), aprobadas.
- Compilacion ESP32 aprobada: 1.048.997 bytes de flash (80%) y 49.968 bytes
  de RAM estatica (15%), con core esp32 2.0.17.
- Validacion en hardware pendiente (sin SIM, sin cobertura y recuperacion).
- Commit relacionado: pendiente.

## [Pro V0.1.27R] - 2026-05-29
### Control de alimentacion I2C por GPIO0 y revision OLED de arranque

- Se documenta GPIO0 como `I2C_POWER_PIN` para enable/alimentacion de OLED y sensores I2C.
- Se documenta secuencia de arranque: GPIO0 LOW, GPIO0 HIGH, inicializacion de `Wire`, deteccion OLED y animacion HIRI.
- La animacion muestra version arriba a la derecha e ID del dispositivo abajo a la derecha.
- Se agrega ventana unica de revision de inicio con estados `WAIT`, `...`, `OK` y `FAIL` para:
  - I2C.
  - OLED.
  - SD.
  - RTC.
  - SHT4.
  - SHT31.
  - ENS.
  - GAS.
- La ventana de revision queda visible cerca de 3 segundos antes de continuar con modem/red.
- Se documenta comando serial `i2c reset` para cortar/reactivar GPIO0, reiniciar bus I2C, redetectar OLED y volver a revisar sensores.
- Se actualiza documentacion a `DEVICE_ID_STR = "12"`, AP `HIRIPRO_12` y mapeo HTTP del dispositivo 12.
- Se corrige el periodo operativo de guardado SD documentado a 180 s / 3 min.

Impacto:

- Cambio documental alineado con firmware actual.
- Reduce riesgo de confundir GPIO0 con boton; GPIO0 queda definido como enable/power-cycle I2C.

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

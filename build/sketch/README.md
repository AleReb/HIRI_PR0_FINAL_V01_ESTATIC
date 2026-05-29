#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\README.md"
# FirmwarePro (HIRI PR0 Estático)

Firmware para estación de monitoreo técnico-científico basada en ESP32, con adquisición de sensores ambientales, registro en SD, telemetría HTTP por módem SIM7600, UI OLED y gestor de archivos por WiFi AP.

## Estado actual

- Plataforma objetivo: **ESP32 Dev Module** (`esp32:esp32:esp32`)
- Sketch principal: **`HIRI_PR0_FINAL_V01_ESTATIC.ino`**
- Versión de firmware en código: **Pro V0.1.15V**
- ID de dispositivo activo: **`DEVICE_ID_STR = "10"`**
- AP WiFi local: **`HIRIPRO_10`**
- Password AP por defecto: **`12345678`**
- Backend HTTP: `http://api-sensores.cmasccp.cl/insertarMedicion`
- APN celular configurado: `flolive.net`

## Funcionalidades principales

- Lectura de **PMS5003** para PM1.0, PM2.5, PM10 y datos T/H cuando el modelo lo entrega.
- Lectura de **SDS198** por `Serial2` para PM100/TSP.
- Lectura de temperatura y humedad con **SHT4x** y fallback/soporte para **SHT31**.
- Lectura de gas por **DFRobot MultiGas I2C**.
- Lectura ambiental **ENS160** para TVOC, eCO2 y AQI.
- Soporte GNSS por SIM7600 con parser NMEA, watchdog y modos configurables, actualmente desactivado por defecto en la configuración de estación.
- Registro en **CSV diario** en tarjeta SD.
- Logs separados para errores (`errors_h<ID>.csv`) y transmisiones HTTP fallidas (`failed_h<ID>.csv`).
- Envío HTTP periódico de mediciones al backend.
- UI local con pantalla OLED, modo debug y control por botones.
- Modo **WiFi SD** con web manager para listar, subir, descargar, renombrar y borrar archivos.
- Captive portal DNS para facilitar acceso al gestor SD desde Windows.
- Persistencia de configuración y contadores con `Preferences`.
- Watchdog, recuperación de módem y backoff ante fallas HTTP repetidas.

## Configuración activa de hardware

La configuración vigente está en `config.h` y corresponde a la variante de prueba HIRIDUST/VALPO:

```text
SIM7600: TX27, RX26, PWRKEY4, DTR32, FLIGHT25
PMS5003: RX18, TX5
SDS198 / Serial2: RX23, TX19
Batería: ADC35
NeoPixel: GPIO12
SD HSPI: SCLK14, MISO2, MOSI15, CS13
BTN1: GPIO39
BTN2: deshabilitado por defecto (-1)
```

Notas importantes:

- `BTN1` usa lectura activa en LOW y requiere resistencia externa según la placa vieja Coyhaique.
- `BTN2` queda deshabilitado mientras `BUTTON_PIN_2` sea `-1`; para habilitarlo se debe asignar un GPIO real en `config.h`.
- La configuración por defecto de estación monta SD automáticamente, inicia autostart, activa modo debug y deja GNSS apagado.

## Configuración operativa por defecto

Definida en `config.ino`:

```text
Guardado SD: cada 30 s
Envío HTTP: cada 300 s (5 min)
Timeout HTTP: 15 s
OLED auto-off: desactivado
LED NeoPixel: habilitado al 50 %
Autostart: activado
Esperar GPS antes de iniciar: desactivado
Auto debug: activado
GNSS: desactivado por defecto
Modo GNSS si se habilita: 15 (todas las constelaciones soportadas)
```

## Archivos principales

- `HIRI_PR0_FINAL_V01_ESTATIC.ino`: setup/loop, estados globales, sensores base, módem y scheduler.
- `config.h` / `config.ino`: pines, estructura de configuración y valores persistentes.
- `sd_card.ino`: CSV diario, errores, transmisiones fallidas y rotación por día.
- `http.ino`: ciclo HTTP por comandos AT.
- `wifi.ino`: AP local, WebServer, DNSServer y gestor de archivos SD.
- `ui.ino`: pantallas OLED, menú, modo debug y botones.
- `gps.ino`: GNSS, parser NMEA, diagnósticos, watchdog y XTRA.
- `rtc.ino`: RTC y sincronización de hora.
- `serial_commands.ino`: consola serial de operación y configuración.
- `helpers.ino`: utilidades compartidas.
- `animacion.ino`: animación de arranque.

## Documentación

- **Manual de Usuario:** `documentacion/MANUAL_USUARIO.md`
- **Manual Técnico:** `documentacion/MANUAL_TECNICO.md`
- **Historial de cambios:** `documentacion/CAMBIOS.md`
- **Guía de desarrollo/versionado:** `documentacion/DEVELOPER_GUIDELINES.md`
- **Estructura de menú:** `documentacion/menu_structure.md`
- **Licencia:** `LICENSE`

## Compilación y carga (Arduino CLI)

Ejecutar desde la raíz del proyecto.

### Compilar

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
```

### Subir a dispositivo

Cambiar `COM5` por el puerto real del ESP32.

```bash
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32 .
```

## Formato CSV

El archivo principal se genera como:

```text
/hiripro<ID>_DD_MM_YYYY.csv
```

Cabecera actual:

```text
ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,sht31Hum,sht4xTempC,sht4xHum,resetReason,pm100,gasPpm,tvoc,eco2,aqi,notas
```

También se crean archivos auxiliares:

- `/errors_h<ID>.csv`: errores estructurados con timestamp, tipo, contexto y mensaje.
- `/failed_h<ID>.csv`: transmisiones HTTP fallidas con timestamp, tipo de error y URL.

## Mapeo de sensores HTTP

El envío HTTP usa `DEVICE_ID_STR` para seleccionar una lista fija de `idsSensores` en `getIdsSensores()`.

Para `DEVICE_ID_STR = "10"`:

```text
1114,1115,1115,1116,1116,1116,1116,1116,1117,1118,1118,1118,1118,1118,1119,1120,1120
```

La lista de `idsVariables` es común para todos los dispositivos soportados:

```text
53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6
```

El payload `valores` se arma en este orden:

```text
SO2/gas, TVOC, eCO2, latitud, longitud, CSQ, velocidad, satélites,
batería, temperatura PMS, humedad PMS, PM1.0, PM2.5, PM10,
PM100 SDS198, temperatura SHT, humedad SHT
```

Los datos faltantes o inválidos se transmiten como `-1` para no romper la URL.

## Resumen de cambios documentados

Cambios incorporados en esta actualización del README:

- Se actualizó la versión documentada desde **Pro V0.0.35** a **Pro V0.1.15V**, que es la versión definida actualmente en el código.
- Se corrigieron los comandos de Arduino CLI para compilar y subir el sketch real del proyecto desde la carpeta actual.
- Se documentó la configuración activa para la variante HIRIDUST/VALPO: `BTN1` en GPIO39, `BTN2` deshabilitado, `Serial2` en RX23/TX19 y SD por HSPI.
- Se agregó el estado operativo por defecto: SD cada 30 segundos, HTTP cada 5 minutos, autostart/debug activos y GNSS apagado por defecto.
- Se incorporaron sensores que no estaban reflejados en el README anterior: SHT4x, gas I2C y ENS160.
- Se actualizó el formato CSV con las columnas nuevas `gasPpm`, `tvoc`, `eco2`, `aqi` y `notas`.
- Se documentaron los archivos auxiliares de trazabilidad: errores y transmisiones HTTP fallidas.
- Se corrigió el valor enviado para datos HTTP faltantes o inválidos: actualmente es `-1`, no `-0`.
- Se agregó el mapeo HTTP activo para `DEVICE_ID_STR = "10"`.

## Disclaimer de responsabilidad

Este proyecto se entrega **"tal cual"** (as-is), sin garantías explícitas ni implícitas de funcionamiento, disponibilidad o aptitud para un propósito específico.

El despliegue en terreno, la seguridad del sistema, la validación de datos y el cumplimiento normativo son responsabilidad del usuario/integrador.

Los autores y colaboradores no se responsabilizan por pérdidas de datos, daños directos o indirectos ni por uso fuera de contexto técnico seguro.

## Licencia de documentación

La documentación de este repositorio se publica bajo:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

- Se permite compartir y adaptar con atribución.
- No se permite uso comercial sin autorización adicional.

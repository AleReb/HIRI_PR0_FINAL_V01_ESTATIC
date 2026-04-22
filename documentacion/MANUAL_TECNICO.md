# FirmwarePro — Manual Técnico

Versión de referencia: **Pro V0.0.35**  
Objetivo: documentación técnica para operación, mantenimiento e integración de producto científico-técnico.

---

## 1) Arquitectura general

Firmware modular en archivos `.ino`:

- `FirmwarePro.ino` → núcleo (setup/loop, estados globales, orquestación)
- `config.h` / `config.ino` → estructura y persistencia de configuración
- `gps.ino` → GNSS (NMEA parsing, watchdog, diagnósticos, XTRA)
- `pms.ino` → PMS5003 + SDS198 + LED PM2.5
- `sd_card.ino` → gestión CSV, failed_tx, headers
- `http.ino` → ciclo HTTP por AT (SIM7600)
- `wifi.ino` → AP + WebServer + DNSServer (captive portal)
- `ui.ino` → menú OLED + botones + pantallas de estado
- `serial_commands.ino` → CLI serial completa
- `rtc.ino` → sincronización RTC y hora de módem
- `helpers.ino` → utilidades, logging y helpers de visualización

---

## 2) Hardware y pines

Definidos en `config.h`:

- SIM7600: TX27, RX26, PWRKEY4, DTR32, FLIGHT25
- PMS5003: RX18, TX5 (SoftwareSerial)
- Batería: ADC35
- Power control: 33
- NeoPixel: 12
- Botones: BTN1=19, BTN2=23
- SD (HSPI): SCLK14, MISO2, MOSI15, CS13

---

## 3) Máquina de estados operativa

Estados funcionales relevantes:

- `streaming` → habilita transmisión HTTP periódica
- `loggingEnabled` → habilita guardado SD periódico
- `wifiModeActive` → habilita modo AP/Web
- `displayState` (`DISP_*`) → subestados de UI

Bucle principal (`loop`):

1. `esp_task_wdt_reset()`
2. Atención de botones (`handleButtonLogic`)
3. Si `wifiModeActive`:
   - `dnsServer.processNextRequest()`
   - `server.handleClient()`
   - refresh de display
   - `return` (prioriza transferencia)
4. Si no hay WiFi activo:
   - GNSS watchdog/diag/debug
   - sensores PMS/SDS198
   - refresh pantalla
   - scheduler SD (`sdSavePeriod`) y HTTP (`httpSendPeriod`)
   - comandos seriales

---

## 4) Subsistema GNSS

Funciones clave (`gps.ino`):

- `gnssBringUp()` → encendido/config GNSS, NMEA 1Hz
- `setGnssAllWithFallback()` → set de constelación con fallback
- `gnssWatchdog()` → hot/warm restart si no hay fix
- `parseNMEA()` + `parseGGA/RMC/VTG/GSA/GSV`
- `gnssDiagTick()` → diagnósticos periódicos
- `gnssDebugPollAsync()` → consultas AT no bloqueantes
- XTRA:
  - `detectAndEnableXtra()`
  - `downloadXtraOnce()`
  - `downloadXtraIfDue()`

Modos GNSS soportados:
- 1 (GPS)
- 3 (GPS+GLONASS)
- 5 (GPS+BEIDOU)
- 7 (GPS+GLONASS+BEIDOU)
- 15 (ALL)

---

## 5) Subsistema SD y trazabilidad

`sd_card.ino`:

- `generateCSVFileName()` → archivo diario por ID
- `writeCSVHeader()` → cabecera controlada
- `saveCSVData()` → registro de muestra + nota one-shot
- `saveFailedTransmission()` → cola forense de fallas HTTP
- `writeErrorLogHeader()` → log estructurado

Formato CSV principal con 23 campos, incluyendo:
- telemetría ambiental
- telemetría GNSS
- salud de red
- estado XTRA
- motivo de reinicio
- `notas`

---

## 6) Subsistema HTTP/Red celular

`http.ino` implementa ciclo robusto por AT:

1. `ensurePdpAndNet()` (PDP + NETOPEN con backoff)
2. `HTTPINIT`
3. `HTTPPARA CID`
4. `HTTPPARA URL`
5. `HTTPACTION=0` (GET)
6. parseo de `+HTTPACTION:`
7. `HTTPREAD` (opcional)
8. `HTTPTERM`

Mecanismos de robustez:
- Backoff en reconexión PDP
- Timeout configurable (`config.httpTimeout`)
- watchdog feed durante espera
- logging estructurado de errores

---

## 7) Modo WiFi AP + File Manager

`wifi.ino`:

- AP local (`WiFi.softAP`)
- IP fija típica 192.168.4.1
- `DNSServer` en puerto 53 (captive portal)
- rutas HTTP:
  - `/`
  - `/download`
  - `/delete`
  - `/rename`
  - `/upload`
  - `/delete_all`
  - `/ip`
  - `/ssid`

Controles:
- saneamiento de nombres de archivo
- validación de SD disponible
- límite de listado

---

## 8) UI y experiencia operativa

`ui.ino`:

- Navegación por 2 botones
- confirmaciones para start/stop
- pantallas diagnósticas: red/RTC/GPS/almacenamiento
- modo full de visualización
- mensajes no bloqueantes (`DISP_MESSAGE`)
- flujo de notas one-shot desde menú Mensajes

---

## 9) Configuración persistente (Preferences)

Namespace `config`:
- `sdAuto`, `sdSavePer`
- `httpPer`, `httpTO`
- `oledOff`, `oledTO`
- `ledEn`, `ledBr`
- `autoStart`, `autoGPS`, `autoGPSTO`
- `gnssMode`

Namespace `system`:
- contadores (`sendCnt`, `sdCnt`)
- `csvFile`
- `streaming`

Namespace `rtc`:
- `syncCnt`

---

## 10) Serial CLI — referencia completa

Implementación en `serial_commands.ino` (entrada `\n`, case-insensitive).

## Ayuda
- `help`, `?`

## RTC/tiempo
- `rtc`

## Contadores/estadística
- `counters`
- `resetcnt`
- `stats`

## SD
- `sdinfo`
- `sdlist`
- `sdnew`
- `sdclear`
- `sdclear confirm`

## Red/modem
- `netinfo`
- `csq`

## Sistema
- `sysinfo`
- `mem`
- `reboot`

## Streaming
- `start`
- `stop`

## Config
- `config`
- `config sd`
- `config http`
- `config display`
- `config power`
- `set sdauto on|off`
- `set sdsave 3|60|600|1200`
- `set httpsend 3|60|600|1200`
- `set httptimeout <5..30>`
- `set oledoff on|off`
- `set oledtime 60|120|180`
- `set led on|off`
- `set ledbright 10|25|50|100`
- `set autostart on|off`
- `set autowaitgps on|off`
- `set autogpsto <60..900>`
- `set gnssmode 1|3|5|7|15`
- `configreset`
- `configsave`

---

## 11) Procedimientos de validación (QA)

## 11.1 Build
- `arduino-cli compile --fqbn esp32:esp32:esp32 FirmwarePro.ino`

## 11.2 Flash
- `arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 FirmwarePro.ino`

## 11.3 Smoke test
1. Boot sin errores en serial
2. SD montada y CSV creado
3. GNSS entrega NMEA/fix
4. HTTP responde 2xx
5. WiFi AP visible y web accesible en `192.168.4.1`
6. Operaciones CRUD de archivos en SD

---

## 12) Riesgos y mejoras sugeridas

- Password AP por defecto fija (endurecer para despliegue)
- Añadir autenticación HTTP para gestión de SD
- Añadir endpoint de salud (`/health`) con métricas internas
- Añadir test de regresión de comandos seriales
- Ajustar política exacta de GNSS durante WiFi exclusivo según consumo/latencia

---

## Apéndice A — Mensajes y logs clave

Prefijos frecuentes:
- `[BOOT]`, `[READY]`
- `[GNSS]`, `[GNSSDIAG]`, `[GSA]`, `[GSV]`
- `[HTTP]`, `[NET]`
- `[SD]`, `[FAILED_TX]`
- `[CONFIG]`, `[SYSTEM]`

---

## Apéndice B — Estructura de archivos SD

- `hiripro<ID>_DD_MM_YYYY.csv`
- `errors_h<ID>.csv`
- `failed_h<ID>.csv`

---

## Apéndice C — Integración de producto

Para empaquetado comercial/científico:

- Definir versión de firmware bloqueada por release
- Mantener matriz HW (sensores/módem compatibles)
- Establecer protocolo de calibración y mantenimiento
- Registrar trazabilidad de lote/dispositivo vía `DEVICE_ID_STR`
- Publicar este manual junto a procedimiento de QA de fábrica

---

## Apéndice D — Disclaimer de responsabilidad

Este firmware y este documento técnico se entregan **"as-is"** (tal cual), sin garantía de funcionamiento ininterrumpido ni adecuación a un caso de uso específico.  
La validación metrológica, seguridad eléctrica, cumplimiento regulatorio y operación final son responsabilidad del integrador/desplegador.  
Los autores no asumen responsabilidad por daños, pérdidas de datos o consecuencias derivadas del uso del sistema.

---

## Apéndice E — Licencia de documentación

Este manual se distribuye bajo:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

Permisos principales:
- Compartir y adaptar con atribución.
- Uso no comercial.

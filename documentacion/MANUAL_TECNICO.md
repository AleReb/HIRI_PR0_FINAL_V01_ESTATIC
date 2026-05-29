# FirmwarePro - Manual Técnico

Versión de referencia: **Pro V0.1.27R**  
Sketch principal: **`HIRI_PR0_FINAL_V01_ESTATIC.ino`**  
Objetivo: documentación técnica para operación, mantenimiento e integración de la estación HIRI PR0 estática.

---

## 1. Arquitectura general

Firmware modular en archivos `.ino`:

- `HIRI_PR0_FINAL_V01_ESTATIC.ino`: núcleo, `setup()`, `loop()`, estados globales, sensores base, módem y scheduler.
- `config.h` / `config.ino`: pines, estructura `SystemConfig`, persistencia y defaults.
- `gps.ino`: GNSS, parser NMEA, watchdog, diagnósticos y XTRA.
- `PMSandSensorEXTRA.ino`: PMS5003, SDS198 y LED PM2.5.
- `sd_card.ino`: CSV diario, errores, failed_tx, headers y rotación por día.
- `http.ino`: ciclo HTTP por comandos AT del SIM7600.
- `wifi.ino`: AP, WebServer, DNSServer y captive portal.
- `ui.ino`: menú OLED, pantallas de estado, botones y modo debug.
- `serial_commands.ino`: CLI serial completa.
- `rtc.ino`: RTC y sincronización de hora.
- `helpers.ino`: utilidades de valores seguros y logging.
- `animacion.ino`: animación de arranque.

---

## 2. Hardware y pines activos

Definidos en `config.h` para la variante HIRIDUST/VALPO:

```text
SIM7600: TX27, RX26, PWRKEY4, DTR32, FLIGHT25
PMS5003: RX18, TX5 (SoftwareSerial)
SDS198 / Serial2: RX23, TX19
Batería: ADC35
NeoPixel: GPIO12
SD HSPI: SCLK14, MISO2, MOSI15, CS13
BTN1: GPIO39
BTN2: deshabilitado por defecto (-1)
I2C enable/power: GPIO0
```

Notas:

- `BTN1` usa lectura activa en LOW y requiere resistencia externa según la placa usada.
- `BTN2` solo se habilita si `BUTTON_PIN_2` se cambia desde `-1` a un GPIO real.
- `I2C_POWER_PIN` usa GPIO0 para controlar alimentación/enable de OLED y sensores I2C. En `setup()` parte en LOW, luego sube a HIGH, espera estabilización y recién después inicializa `Wire`.
- `POWER_PIN` existe como referencia histórica, pero no está activo en el flujo actual.

### Secuencia de alimentación I2C y OLED

1. `pinMode(I2C_POWER_PIN, OUTPUT)`.
2. `I2C_POWER_PIN = LOW` durante una ventana breve.
3. `I2C_POWER_PIN = HIGH`.
4. Espera de estabilización.
5. `configureI2CBus()` con `Wire.setTimeOut(50)` y reloj I2C a 50 kHz.
6. Detección de OLED en `0x3C` o `0x3D`.
7. `u8g2.begin()` y animación de arranque.
8. Ventana única de revisión de inicio.

La recuperación `recoverI2CBus()` ejecuta el mismo power-cycle sobre GPIO0, reinicia `Wire`, redetecta OLED y vuelve a inicializar sensores.

---

## 3. Configuración persistente

Namespace `config`:

- `sdAuto`, `sdSavePer`
- `httpPer`, `httpTO`
- `oledOff`, `oledTO`
- `ledEn`, `ledBr`
- `autoStart`, `autoGPS`, `autoGPSTO`, `autoDbg`
- `rotDisp`
- `gnssEn`, `gnssMode`

Defaults vigentes:

```text
sdAutoMount = true
sdSavePeriod = 180000 ms
httpSendPeriod = 300000 ms
httpTimeout = 15 s
oledAutoOff = false
ledEnabled = true
ledBrightness = 50 %
autostart = true
autostartWaitGps = false
autoDebug = true
rotateDisplay = false en configSetDefaults()
gnssEnabled = false
gnssMode = 15
```

Namespace `system`:

- `sendCnt`
- `sdCnt`
- `csvFile`
- `streaming`

Namespace `rtc`:

- `syncCnt`

---

## 4. Máquina de estados operativa

Estados funcionales relevantes:

- `streaming`: habilita transmisión HTTP periódica.
- `loggingEnabled`: habilita guardado SD periódico.
- `wifiModeActive`: habilita modo AP/Web.
- `uiFullMode`: activa vista completa/modo debug.
- `displayState`: subestados de UI (`DISP_NORMAL`, `DISP_MESSAGE`, `DISP_PROMPT`, `DISP_NETWORK`, `DISP_RTC`, `DISP_STORAGE`, `DISP_GPS`, `DISP_WIFI`).

Bucle principal:

1. Alimenta watchdog.
2. Procesa botones.
3. Si `wifiModeActive`, atiende DNS/WebServer, refresca display y retorna.
4. Si no hay WiFi activo:
   - Ejecuta GNSS solo si `config.gnssEnabled`.
   - Actualiza sensores.
   - Lee PMS/SDS198.
   - Actualiza LED PM2.5.
   - Atiende AT async.
   - Muestrea batería.
   - Refresca OLED.
   - Ejecuta watchdog de red, backoff HTTP y health log.
   - Guarda SD según `sdSavePeriod`.
   - Envía HTTP según `httpSendPeriod`.
   - Atiende comandos seriales.

---

## 5. Sensores

Sensores actuales:

- PMS5003: PM1.0, PM2.5, PM10, temperatura y humedad si están disponibles.
- SDS198: PM100/TSP por `Serial2`.
- SHT4x: temperatura y humedad externa principal.
- SHT31: temperatura y humedad alternativa/compatibilidad.
- DFRobot MultiGas I2C: concentración de gas.
- ENS160: TVOC, eCO2, AQI y validación de estado.
- RTC DS3231: tiempo y temperatura interna.
- ADC batería: promedio suavizado.

Mecanismos de robustez:

- Revisión secuencial de arranque por sensor con estado en Serial y OLED.
- Recuperación de bus I2C ante fallas repetidas.
- Power-cycle de OLED/sensores I2C usando GPIO0.
- Reintento/reinicialización para SHT4x y ENS160.
- Valores seguros para HTTP cuando una lectura no está disponible.

Durante el arranque se muestra una sola ventana de revisión con filas:

```text
I2C, OLED, SD, RTC, SHT4, SHT31, ENS, GAS
```

Cada fila se actualiza de `WAIT` a `...` y finalmente a `OK` o `FAIL`. Al terminar, la ventana queda visible cerca de 3 segundos.

---

## 6. GNSS

El subsistema GNSS existe, pero en la configuración de estación está **desactivado por defecto** (`gnssEnabled = false`).

Funciones clave:

- `gnssBringUp()`
- `setGnssAllWithFallback()`
- `gnssWatchdog()`
- `parseNMEA()`, `parseGGA()`, `parseRMC()`, `parseVTG()`, `parseGSA()`, `parseGSV()`
- `gnssDiagTick()`
- `gnssDebugPollAsync()`
- `detectAndEnableXtra()`
- `downloadXtraOnce()`
- `downloadXtraIfDue()`

Modos GNSS soportados:

- `1`: GPS
- `3`: GPS + GLONASS
- `5`: GPS + BEIDOU
- `7`: GPS + GLONASS + BEIDOU
- `15`: todas las constelaciones soportadas

---

## 7. SD y trazabilidad

`sd_card.ino` implementa:

- `generateCSVFileName()`: archivo diario por ID.
- `writeCSVHeader()`: cabecera controlada.
- `saveCSVData()`: registro de muestra y nota one-shot.
- `saveFailedTransmission()`: registro forense de fallas HTTP.
- `writeErrorLogHeader()`: log estructurado de errores.

Formato del CSV principal:

```text
ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,sht31Hum,sht4xTempC,sht4xHum,resetReason,pm100,gasPpm,tvoc,eco2,aqi,notas
```

Archivos:

- `/hiripro<ID>_DD_MM_YYYY.csv`
- `/errors_h<ID>.csv`
- `/failed_h<ID>.csv`

---

## 8. HTTP y red celular

Endpoint:

```text
http://api-sensores.cmasccp.cl/insertarMedicion
```

APN:

```text
flolive.net
```

Flujo HTTP:

1. Validación PDP/red.
2. `HTTPINIT`.
3. `HTTPPARA CID`.
4. `HTTPPARA URL`.
5. `HTTPACTION=0`.
6. Parseo de `+HTTPACTION:`.
7. `HTTPREAD` opcional.
8. `HTTPTERM`.

Mecanismos de robustez:

- Timeout configurable.
- Watchdog feed durante esperas.
- Registro de errores.
- Registro de URL fallida en SD.
- Contador de fallas consecutivas.
- Backoff HTTP tras fallas repetidas.
- Recuperación de módem.

---

## 9. Mapeo HTTP de sensores

`DEVICE_ID_STR` activo:

```text
12
```

`idsSensores` para dispositivo 12:

```text
1128,1129,1129,1130,1130,1130,1130,1130,1131,1132,1132,1132,1132,1132,1133,1134,1134
```

`idsVariables` común:

```text
53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6
```

Orden de `valores`:

```text
SO2/gas, TVOC, eCO2, latitud, longitud, CSQ, velocidad, satélites,
batería, temperatura PMS, humedad PMS, PM1.0, PM2.5, PM10,
PM100 SDS198, temperatura SHT, humedad SHT
```

Los datos faltantes o inválidos se envían como `-1`.

---

## 10. WiFi AP y gestor SD

`wifi.ino` implementa:

- `WiFi.softAP`.
- IP típica `192.168.4.1`.
- `DNSServer` en puerto 53.
- WebServer en puerto 80.

Rutas principales:

- `/`
- `/download`
- `/delete`
- `/rename`
- `/upload`
- `/delete_all`
- `/ip`
- `/ssid`

Controles:

- Sanitización básica de nombres.
- Validación de SD disponible.
- Límite de listado.
- Modo WiFi exclusivo para priorizar transferencias.

---

## 11. UI y botones

`ui.ino` implementa:

- Navegación por menú OLED.
- Confirmación para iniciar/detener muestreo.
- Pantallas de red, RTC, GPS y almacenamiento.
- Modo debug/full de telemetría.
- Pantalla WiFi dedicada.
- Notas one-shot desde menú Mensajes.

Controles lógicos:

- BTN1: navegar/cancelar; en modo debug ejecuta acción rápida de muestreo/transmisión.
- BTN2: seleccionar/confirmar; en modo debug sale al menú; en pantalla WiFi alterna WiFi SD.

En hardware actual, BTN2 está deshabilitado por defecto hasta asignar un GPIO real.

---

## 12. Serial CLI

Implementación en `serial_commands.ino`, entrada con `\n`, case-insensitive.

Comandos:

- `help`, `?`
- `rtc`, `rtcsync`, `modemtime`
- `counters`, `resetcnt`, `stats`
- `sdinfo`, `sdlist`, `sdnew`, `sdclear`, `sdclear confirm`
- `netinfo`, `csq`
- `sysinfo`, `mem`, `reboot`
- `i2c reset`
- `start`, `stop`
- `config`, `config sd`, `config http`, `config display`, `config power`
- `set sdauto on|off`
- `set sdsave 3|60|600|1200`
- `set httpsend 3|60|600|1200`
- `set httptimeout 5-30`
- `set oledoff on|off`
- `set oledtime 60|120|180`
- `set led on|off`
- `set ledbright 10|25|50|100`
- `set autostart on|off`
- `set gnss on|off`
- `set autowaitgps on|off`
- `set autogpsto 60-900`
- `set autodebug on|off`
- `set rotatedisplay on|off`
- `set gnssmode 1|3|5|7|15`
- `configreset`
- `configsave`

---

## 13. Validación QA

### Build

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
```

### Flash

```bash
arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 .
```

### Smoke test

1. Boot sin loop de reset.
2. GPIO0 sube a HIGH antes de inicializar I2C.
3. OLED muestra animación con versión e ID.
4. Ventana de revisión muestra `I2C`, `OLED`, `SD`, `RTC`, `SHT4`, `SHT31`, `ENS` y `GAS` con `OK/FAIL`.
5. SD monta y crea CSV.
6. Sensores entregan valores o fallan con logs controlados.
5. Módem registra red y obtiene CSQ.
6. HTTP responde o deja trazabilidad en `failed_h<ID>.csv`.
7. WiFi AP aparece como `HIRIPRO_12`.
8. Web SD responde en `192.168.4.1`.

Si GNSS se habilita, agregar validación de fix, satélites, HDOP y NMEA.

---

## 14. Riesgos y mejoras sugeridas

- Password AP por defecto fija.
- Falta autenticación HTTP para gestión SD.
- GNSS apagado por defecto puede producir campos de ubicación inválidos si no se habilita explícitamente.
- BTN2 deshabilitado requiere validación de flujo de UI según hardware final.
- Conviene agregar endpoint `/health`.
- Conviene automatizar pruebas de comandos seriales.
- Validar calibración de sensores antes de despliegue formal.

---

## Apéndice A. Logs frecuentes

- `[BOOT]`, `[READY]`
- `[MODEM]`, `[NET]`
- `[GNSS]`, `[GNSSDIAG]`, `[GSA]`, `[GSV]`
- `[HTTP]`
- `[SD]`, `[FAILED_TX]`
- `[CONFIG]`, `[SYSTEM]`
- `[UI]`

---

## Apéndice B. Disclaimer

Este firmware y este documento técnico se entregan **"as-is"** (tal cual), sin garantía de funcionamiento ininterrumpido ni adecuación a un caso de uso específico.

La validación metrológica, seguridad eléctrica, cumplimiento regulatorio y operación final son responsabilidad del integrador/desplegador.

Los autores no asumen responsabilidad por daños, pérdidas de datos o consecuencias derivadas del uso del sistema.

---

## Apéndice C. Licencia de documentación

Este manual se distribuye bajo:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

Permisos principales:

- Compartir y adaptar con atribución.
- Uso no comercial.

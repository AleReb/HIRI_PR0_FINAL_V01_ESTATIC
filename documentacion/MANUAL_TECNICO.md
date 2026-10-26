# FirmwarePro — Manual técnico

Referencia: **Pro V0.1.29R**. Revisión del código: **2026-10-26**.
Placa: `esp32:esp32:esp32`; sketch: `HIRI_PR0_FINAL_V01_ESTATIC.ino`.
Configuración de esta copia: **ID 3**, AP **HIRIPRO_3**, APN **gigsky-02**.

## 1. Arquitectura y fuentes de verdad

| Archivo | Responsabilidad |
|---|---|
| Sketch principal | Estados, setup/loop, AT, sensores I²C y construcción del payload |
| `config.h` | Pines y `SystemConfig` |
| `config.ino` | Preferencias, defaults y utilidades SD/LED |
| `http.ino` | Conexión celular con reintentos y petición HTTP(S) por AT |
| `sd_card.ino` | Cabeceras, muestras CSV y transmisiones fallidas |
| `PMSandSensorEXTRA.ino` | Parseo PMS/SDS198 y LED de PM2.5 |
| `ui.ino` | OLED, menú, notas y botones |
| `serial_commands.ino` | Consola de operación y configuración |
| `wifi.ino` | AP, DNS y gestor de archivos |
| `gps.ino` | Parseo NMEA, diagnóstico, recuperación GNSS y utilidades XTRA |
| `rtc.ino` | Lectura/sincronización de hora y utilidades RTC |
| `helpers.ino` | Sanitización de valores, motivo de reset y errores |
| `animacion.ino` | Animación de inicio |

Los comentarios históricos y funciones definidas no implican que estén
conectados al flujo de ejecución. Esta revisión distingue llamadas efectivas,
parámetros almacenados y utilidades disponibles.

## 2. Hardware y alimentación I²C

| Elemento | Configuración del código |
|---|---|
| SIM7600 / Serial1 | TX27, RX26; PWRKEY4, FLIGHT25; DTR32 definido |
| Plantower / SoftwareSerial | Macros `pms_RX=18`, `pms_TX=5`; ver observación inferior |
| SDS198 / Serial2 | RX23, TX19; 9600 baud |
| SD / HSPI | SCLK14, MISO2, MOSI15, CS13 |
| Batería | ADC35 |
| NeoPixel | GPIO12, un LED |
| BTN1 | GPIO39, activo en LOW; resistencia externa según placa |
| BTN2 | Deshabilitado: `BUTTON_PIN_2=-1` |
| Enable/alimentación I²C | GPIO0, activo en HIGH |

**Observación de cableado PMS:** el objeto se crea como
`SoftwareSerial pms(pms_TX, pms_RX)`. Con la biblioteca EspSoftwareSerial
instalada, cuyo constructor recibe primero RX y luego TX, esto configura
**RX en GPIO5 y TX en GPIO18**, a diferencia de los nombres de las macros.
Verificar el cableado real antes de usar esas etiquetas como guía.
Esta revisión documental no cambia los pines del firmware.

GPIO0 se configura como salida, permanece LOW durante **250 ms** y luego
pasa a HIGH; se esperan **700 ms** antes de continuar el arranque
(hay además un retardo de 300 ms antes de `configureI2CBus()`).
`Wire.begin()` usa los pines predeterminados de la placa: el sketch no fija
SDA/SCL explícitos. El bus se configura a **50 kHz** con timeout de **50 ms**.

El enable queda HIGH durante la operación, también sin SIM o sin red.
`recoverI2CBus()` y el comando serial `i2c reset` lo bajan temporalmente y
lo vuelven a activar para recuperar OLED y sensores. GPIO0 controla un circuito
externo de enable/alimentación; la configuración del software no confirma
por sí sola la tensión física de la placa.

`POWER_PIN=33` y `MODEM_DTR=32` están definidos, pero no se accionan en el
flujo actual. El control activo de alimentación I²C es `I2C_POWER_PIN=0`.

## 3. Configuración persistente y reglas de arranque

| Parámetro | Comportamiento actual |
|---|---|
| SD | Se intenta montar al arrancar; guardado cada 180000 ms |
| HTTP | Turnos cada 300000 ms; primer turno programado inmediatamente |
| Timeout de espera HTTPACTION | 15 s si no hay otro valor persistido |
| Autostart | Se fuerza a activado en cada arranque |
| Esperar GPS | Se fuerza a desactivado en cada arranque |
| Auto debug | Se fuerza a activado en cada arranque |
| OLED auto-off | Desactivado si no hay valor persistido |
| LED | Habilitado al 50 % si no hay valor persistido |
| GNSS | Desactivado si no hay valor persistido; modo 15 si se habilita |
| Rotación OLED | `loadConfig()` usa `true` sin valor guardado; `configSetDefaults()` usa `false` |
| Reinicio programado | Valor configurado de 3 h por defecto; no hay ejecución del temporizador en `loop()` |

`enforceStationStartupConfig()` restablece SD automática, periodos SD/HTTP,
autostart, no esperar GPS y auto debug al arrancar. Los comandos seriales
pueden cambiar algunos valores durante la sesión, pero esos parámetros
se vuelven a imponer en el siguiente inicio. Los comandos `set ...`
guardan automáticamente en `Preferences`.

Claves del namespace `config`:

```text
sdAuto, sdSavePer, httpPer, httpTO, oledOff, oledTO,
ledEn, ledBr, autoStart, autoGPS, autoGPSTO, autoDbg,
rotDisp, rebootH, gnssEn, gnssMode
```

En `system` se leen `sendCnt`, `csvFile` y `streaming`.
El contador HTTP se persiste al enviar con éxito. `sdSaveCounter` se
incrementa en RAM: no se restaura desde `sdCnt` al arrancar, aunque
`resetcnt` escriba esa clave. El estado `streaming` anterior se lee,
pero el autostart impuesto inicia la sesión independientemente de ese estado.
Las funciones RTC usan `rtc/syncCnt` para el contador de sincronizaciones.

## 4. Arranque y scheduler local

Orden efectivo de `setup()`:

1. Inicia Serial a 115200 y watchdog de tarea de 60 s.
2. Ejecuta secuencia GPIO0 e inicializa I²C.
3. Imprime versión y motivo/etapa previa de reinicio.
4. Carga preferencias y aplica `enforceStationStartupConfig()`.
5. Monta SD e intenta crear la cabecera del archivo.
6. Detecta OLED y ejecuta animación; inicia UART PMS/SDS198, RTC y sensores.
7. Muestra revisión de arranque durante unos 3 s.
8. Habilita autostart: `streaming=true`, `loggingEnabled=SDOK`.
9. Marca adquisición local lista, antes de abrir Serial1 y probar módem.
10. Realiza hasta tres pruebas AT de 5 s cada una, con pulsos PWRKEY tras fallo.
11. Si responde, configura ahorro del módem y llama utilidades XTRA/GNSS;
    estas respetan `gnssEnabled`. Si no responde, omite ese inicio.
12. Activa auto debug; finaliza setup.

El primer turno HTTP queda vencido a propósito para ejecutarse en el primer
loop normal. El temporizador SD parte al habilitar autostart y espera 180 s.

`serviceLocalData()` agrupa sensores cada 2 s, ingestión PMS/SDS198,
LED cada 300 ms, muestreo de batería y guardado SD según periodo.
Se llama desde el loop y desde las esperas AT; incluye guardia de reentrada.
Los periodos son atendidos cooperativamente, no por interrupciones que
garanticen tiempo exacto.

En operación normal, el loop atiende botones, GNSS si el módem está disponible,
servicio local, información inicial de red, AT, display, turno HTTP y consola.
En WiFi activo, atiende DNS/WebServer y retorna antes de esas tareas normales;
no debe describirse como registro local continuo durante gestión WiFi.

## 5. Conexión celular y operación sin red

El registro local se habilita antes de inicializar el módem. En operación normal,
con SD montada y `loggingEnabled=true`, se guardan muestras cada **180 segundos
(3 minutos)** aunque falten SIM, cobertura, conexión de datos o GPS. La primera
muestra periódica se guarda al vencer ese intervalo; no se escribe una muestra
inmediata por el solo hecho de arrancar.

| Situación | Acción del firmware |
|---|---|
| Módem sin respuesta AT | Falla el intento; no transmite HTTP. |
| SIM ausente, bloqueada o no lista | Exige `+CPIN: READY`; si no lo obtiene, falla el intento sin transmitir. |
| Sin registro celular | Consulta `CEREG/CGREG` durante una ventana de unos 15 s por intento; si falla, no transmite. |
| No abre la conexión de datos | Espera confirmación `+NETOPEN: 0`, hasta 30 s, y verifica `NETOPEN?`; si falla, no transmite. |
| Fallan los tres intentos de conexión | Mantiene registro local y bloquea nuevos intentos de conexión durante una hora. |
| Vuelve la conexión | Reanuda el envío de la muestra actual; no reenvía el historial CSV. |
| Conexión de datos abierta pero servidor inaccesible o respuesta HTTP errónea | Registra el fallo y vuelve a probar en el siguiente turno HTTP; este caso no activa la pausa de una hora. |

Cada ciclo permite **hasta 3 intentos en total**, con **5 s entre intentos**.
La pausa empieza después del tercer fallo. Al vencer la hora se habilita otro
ciclo al siguiente turno de envío (normalmente cada 5 minutos); no es una alarma
exacta a los 60 minutos. Una acción manual de envío también pasa por esa protección.

Las esperas AT y las pausas entre intentos atienden sensores, SD y watchdog.
La pausa de reconexión se conserva solo en RAM: un reinicio la pierde y el
arranque vuelve a programar un intento inmediato.

`[HTTP] Send`, `[HTTP] GET ...` y `[HTTP][SYNC] URL length = ...`
aparecen antes de comprobar la conexión. No prueban una transmisión.
El comando que ejecuta la petición, `AT+HTTPACTION=0`, solo se alcanza después
de validar la conexión y completar la preparación HTTP.

Flujo interno:

1. `connectCellularOnce()` prueba AT (1 s).
2. Exige SIM lista (`+CPIN?`, 2 s).
3. Consulta registro propio/roaming en `CEREG/CGREG` (estados 1 o 5).
4. Si `NETOPEN?` ya confirma apertura, devuelve éxito.
5. Configura APN, autenticación si hay usuario y parámetros TCP.
6. Espera resultado de apertura, y vuelve a comprobar `NETOPEN?`.
7. `ensurePdpAndNet()` agrupa los tres intentos y aplica pausa solo al
   agotarlos. Durante la pausa devuelve fallo sin enviar comandos de conexión.

La ventana de registro es de 15 s más el tiempo de finalizar una consulta
en curso; no es un límite total de todo el ciclo. Los comandos tienen
timeouts individuales. No se usa `modem.gprsConnect()` en esta ruta.
`hasRed` refleja el último resultado de conexión, no una prueba continua de
Internet. La información de operador/CSQ del loop se consulta al inicio;
no debe usarse como monitor permanente de conectividad.

## 6. Petición HTTP(S)

`sendCurrentMeasurement()` construye la URL y rechaza IDs sin mapeo.
`httpGet_webhook()` ejecuta:

1. Validación mediante `ensurePdpAndNet()`; ante fallo retorna antes de HTTP.
2. `HTTPTERM` preventivo, `HTTPINIT`, parámetros CID y URL.
3. `HTTPACTION=0`, espera de resultado y parseo de código/tamaño.
4. `HTTPREAD` si hay contenido, seguido de `HTTPTERM`.
5. Éxito solo para códigos 2xx; incremento persistido de `sendCounter`.

`atRun()` y `sendAtSync()` atienden watchdog y adquisición local mientras
esperan. La espera de HTTPACTION usa `config.httpTimeout` (15 s por defecto);
el estado AT tiene además un timeout de 90 s. Un timeout, rechazo del servidor
o fallo de preparación produce un resultado falso y trazabilidad, no un
reinicio explícito.

Las variables `httpConsecutiveFailCount`, `httpBackoffActive` y constantes
relacionadas existen, pero **no están aplicadas al control de envíos**.
No hay pausa de una hora por respuestas HTTP fallidas. Para comprobar una
caída del servidor se intenta una petición si la conexión celular está abierta.

## 7. Formatos y mapeos del backend

### Equipos 1 a 13

Usan `http://api-sensores.cmasccp.cl/insertarMedicion` y 17 valores.
Para el ID activo **3**, `getIdsSensores()` devuelve:

```text
1048,1049,1049,1050,1050,1050,1050,1050,1051,1052,1052,1052,1052,1052,1053,1054,1054
```

`idsVariables`:

```text
53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6
```

Orden de valores: gas, TVOC, eCO2, latitud, longitud, CSQ, velocidad,
satélites, batería, temperatura PMS, humedad PMS, PM1, PM2.5, PM10,
PM100 SDS198, temperatura SHT y humedad SHT. Para los últimos dos se
prefiere SHT4x y se usa SHT31 como alternativa si corresponde.

### Formato estático desde el equipo 14

Usa `https://api-sensores.cmasccp.cl/insertarMedicion` y siete valores.
Actualmente solo está definido el mapeo del **equipo 14**:

| Posición | Sensor backend | Variable | Lectura |
|---|---|---|---|
| 1 | 1232 | 3 | Temperatura Plantower, °C |
| 2 | 1232 | 6 | Humedad Plantower, % |
| 3 | 1232 | 7 | PM1, µg/m³ |
| 4 | 1232 | 8 | PM2.5, µg/m³ |
| 5 | 1232 | 9 | PM10, µg/m³ |
| 6 | 1233 | 3 | Temperatura SHT41, °C |
| 7 | 1233 | 6 | Humedad SHT41, % |

```text
idsSensores=1232,1232,1232,1232,1232,1233,1233
idsVariables=3,6,7,8,9,3,6
```

`valores` contiene lecturas numéricas, sin las descripciones ni las unidades.
Si SHT4x no está disponible, sus dos posiciones se envían como `-1`;
el formato estático no lo reemplaza por SHT31. Los flotantes no finitos se
envían como `-1`. Los valores PM son enteros y pueden ser cero; no existe
una comprobación de antigüedad de la muestra para todos los campos.

Cambiar `DEVICE_ID_STR` a `"14"` selecciona ese mapeo. Cada equipo posterior
requiere sus IDs reales en `getIdsSensores()`: no se extrapolan ni se reutilizan
los del 14. Un ID sin mapeo no envía y registra `HTTP_MAPPING_MISSING`.
El CSV de SD conserva sus 27 columnas para ambos formatos de telemetría.

## 8. SD, fecha y calidad de datos

Cabecera literal de `writeCSVHeader()` (**27 columnas**):

```csv
ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht4xTempC,sht4xHum,resetReason,pm100,gasPpm,tvoc,eco2,aqi,notas
```

- Archivo principal: `/hiripro<ID>_DD_MM_YYYY.csv`.
- Errores: `/errors_h<ID>.csv`, esquema `timestamp,type,context,message`.
- Fallos de envío: `/failed_h<ID>.csv`, esquema `timestamp,error_type,url`.

`saveCSVData()` exige `SDOK && loggingEnabled`. Cambia de nombre al detectar
otro día del mes y persiste `csvFile`. Cierra cada escritura y limpia la nota
solo después de abrir/escribir el archivo. La nota inicial es `"9"`;
posteriormente puede ser una nota de menú o quedar vacía.

La cabecera solo se escribe si el archivo está vacío. `sdnew` vuelve a generar
el nombre diario y puede abrir el mismo archivo; no garantiza una nueva
cabecera. Tras cambiar el esquema, respaldar y comprobar archivos existentes.

El CSV no usa una política universal `-1`: PMS T/HR con NaN se guardan como
`0`, gas/ENS no disponibles como `0`, SHT4x pueden ser NaN o último valor.
Los PM pueden conservar valores anteriores o cero; no hay caducidad universal
de muestras. No hay columnas SHT31 en el CSV actual.

Sin RTC válido para lectura (`rtcOK=false`), CSV/nombre usan fecha
`2000-01-01`. El primer intento de cabecera del setup ocurre antes de
`rtc.begin()`; la primera muestra vuelve a seleccionar nombre al detectar el
día. `syncRtcSmart()` existe, pero no se llama desde setup/loop actual:
no prometer inicialización automática de RTC por hora de compilación.
`saveFailedTransmission()` llama `rtc.now()` sin comprobar `rtcOK`;
la robustez de ese log sin RTC requiere una corrección y prueba aparte.

## 9. GNSS y RTC

GNSS está apagado salvo preferencia persistida en contrario. `set gnss on`
guarda la preferencia y requiere reiniciar para el inicio completo.
Modos válidos: 1 (GPS), 3 (GPS+GLONASS), 5 (GPS+BeiDou),
7 (GPS+GLONASS+BeiDou), 15 (todas las soportadas).

Con GNSS activo hay parseo NMEA, diagnósticos y reinicios HOT/WARM por
tiempo sin fix. La adquisición local no espera fix.
`downloadXtraIfDue()` y `syncRtcSmart()` están definidos, pero no llamados
por el scheduler actual. La detección/habilitación XTRA no equivale a una
descarga periódica ejecutándose. `downloadXtraOnce()`, si se llama, usa
la misma protección de conexión.

Para sincronización manual usar `rtc`, `modemtime` y `rtcsync`.
La disponibilidad de hora del módem se comprueba al ejecutar esas utilidades.

## 10. WiFi y UI

AP: `HIRIPRO_3`, contraseña `12345678`, IP `192.168.4.1`;
canal 1, hasta dos clientes. El inicio del AP exige SD y credenciales válidas.
DNS cautivo usa puerto 53; WebServer usa 80.

| Método | Ruta |
|---|---|
| GET | `/`, `/download`, `/ip`, `/ssid` |
| POST | `/delete`, `/rename`, `/upload`, `/delete_all` |

No hay autenticación de aplicación para el gestor; usar el AP en un entorno
controlado y respaldar antes de borrar. La operación WiFi es exclusiva respecto
del ciclo normal, incluido el procesamiento de consola.

En auto debug, BTN1 ejecuta envío manual y después intenta una escritura SD,
habilitando el logging solo temporalmente para esa escritura si la SD está
disponible. No alterna el muestreo. BTN2 sale al menú, pero actualmente no
tiene GPIO asignado. Los menús y sus condiciones figuran en
[menu_structure.md](menu_structure.md).

## 11. Consola y diferencias de control

Consultar el catálogo en el [Manual de usuario](MANUAL_USUARIO.md).
Puntos que afectan operación:

- `start`, si estaba detenido, activa streaming con logging apagado.
- `stop` apaga ambas tareas si estaban en marcha.
- Iniciar desde el menú sí intenta montar SD y activa logging según `SDOK`.
- Los `set` guardan automáticamente. `configsave` permite guardar el estado.
- `set reboot 0..720` almacena horas, pero no hay ejecución del reinicio
  programado. Los reinicios explícitos existentes son `reboot` y menú Reiniciar.
- `set sdsave 3` significa **3 segundos**, no 3 minutos.
- Varias preferencias son impuestas nuevamente al arrancar, según sección 3.

## 12. Validación y diagnóstico

Compilar y cargar desde la raíz:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 .
```

Dependencias utilizadas en la compilación registrada: core ESP32 2.0.17,
TinyGSM 0.12.0, Adafruit NeoPixel, SHT31, SHT4x, BusIO y Unified Sensor,
DFRobot ENS160/MultiGasSensor, EspSoftwareSerial, RTClib y U8g2.
WiFi/WebServer/DNSServer/Wire/SPI/SD/Preferences proceden del core.

Simulación de funciones reales con dobles de hardware:

```powershell
python tests/test_offline_logging.py
python tests/test_measurement_payload.py
```

Se comprobaron ausencia de módem/SIM, SIM bloqueada, falta de registro,
fallo PDP, conexión y recuperación, pausa con guardado, esperas AT y
desbordamiento de `millis()`. También se validan orden de siete valores,
compatibilidad de IDs 1–13, datos ausentes y rechazo de IDs desconocidos.
Estas pruebas no comprueban cableado, escritura física SD, consumo ni TLS real.

En placa comprobar arranque y GPIO0, CSV/cabecera, una hora sin SIM/cobertura,
recuperación de red, servidor no disponible y entrada/salida WiFi.
Para reinicios capturar `[BOOT]`, `[RESET] Reason`, etapa previa y últimas
líneas de conexión. El reporte del usuario sugiere energía como causa del
incidente; no hay registro de reset que permita confirmarla aquí.

La compilación registrada de Pro V0.1.29R fue correcta (80 % flash, 15 % RAM).
La recepción real HTTPS y la validación eléctrica siguen pendientes.
No marcar una compilación o simulación como validación completa en terreno.

## 13. Limitaciones pendientes de implementación

Esta revisión documenta el código y no cambia su comportamiento:

- Pausa de una hora solo por conexión celular; no por fallos HTTP/servidor.
- Pausa perdida al reiniciar; se programa intento HTTP inmediato al arrancar.
- WiFi SD suspende registro periódico y consola.
- `start` serial no habilita logging.
- Constructor PMS y nombres de macros no coinciden en RX/TX.
- Reinicio por horas configurado pero sin ejecución en loop.
- Inicialización inteligente RTC y descarga periódica XTRA no conectadas al scheduler.
- Marcadores de datos ausentes y antigüedad de muestras no son uniformes.

## Responsabilidad y licencia

El firmware y este manual se entregan tal cual, sin garantía de operación
ininterrumpida o adecuación a un uso particular. Validar calidad de datos,
integración eléctrica y funcionamiento en terreno antes del despliegue.

Documentación bajo [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/).
Consultar [LICENSE](../LICENSE).

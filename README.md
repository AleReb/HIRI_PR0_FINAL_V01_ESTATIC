# FirmwarePro — HIRI PR0 estático

Firmware ESP32 para adquisición ambiental, CSV local en SD, telemetría celular
por SIM7600, OLED y gestión de archivos mediante WiFi AP.

## Estado verificado

Revisión documental: **2026-10-26**. Valores leídos del código de trabajo:

| Dato | Valor |
|---|---|
| Firmware | **Pro V0.1.30R** |
| Sketch | `HIRI_PR0_FINAL_V01_ESTATIC.ino` |
| Placa | ESP32 Dev Module, `esp32:esp32:esp32` |
| ID activo | `DEVICE_ID_STR = "1AE"` |
| AP WiFi | `HIRIPRO_1AE` |
| Contraseña AP | `12345678` |
| IP del gestor SD | `192.168.4.1` |
| APN celular | `flolive.net` |

El ID y el APN son parámetros de compilación del sketch. Antes de cargar otro
equipo, revisar esos valores y su mapeo de sensores; los datos de esta tabla
describen esta copia del código, no identifican todos los equipos desplegados.

## Operación sin SIM, cobertura o Internet

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

El guardado requiere SD operativa y registro habilitado. El comando `stop`
desactiva registro y envío; `start` por serial **no reactiva la SD**.
Además, el modo WiFi SD pausa el ciclo normal de adquisición, guardado y HTTP
mientras atiende el gestor de archivos.

## Hardware y enable I²C

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

## Arranque y sensores

Después de activar I²C se cargan las preferencias, se monta la SD y se detecta
OLED en `0x3C/0x3D`. Se muestra la animación con versión e ID y la ventana
de diagnóstico `I2C/OLED/SD/RTC/SHT4/SHT31/ENS/GAS`, retenida unos 3 s.

Se inicializan PMS/Serial2, RTC y sensores I²C antes de habilitar la adquisición
local. El módem recibe hasta tres pruebas AT de arranque; si no responde,
se conserva el registro local y se omite el inicio GNSS. La conexión celular
se comprueba desde la ruta de envío HTTP.

El código incluye PMS5003/Plantower, SDS198, SHT4x/SHT41, SHT31, MultiGas y
ENS160, además de RTC DS3231 y batería. El formato de siete valores de la serie AE cambia la telemetría; no elimina las comprobaciones de los otros sensores.

## Configuración y controles

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

En auto debug, **BTN1 intenta un envío manual y un guardado SD**, respetando la
protección de conexión. **BTN2 sale al menú**, pero está deshabilitado en esta
configuración física. Para iniciar/detener desde el menú se necesita BTN2.

`start` por serial inicia solo transmisión con `loggingEnabled=false` si
estaba detenida. `stop` apaga ambas tareas. El arranque automático y la acción
de inicio del menú habilitan SD si está montada. Ver
[Manual de usuario](documentacion/MANUAL_USUARIO.md) y
[estructura del menú](documentacion/menu_structure.md).

## Datos locales en SD

Archivo principal: `/hiripro<ID>_DD_MM_YYYY.csv`. Para el equipo activo,
prefijo `hiripro1AE`. Cabecera real de **27 columnas**:

```csv
ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht4xTempC,sht4xHum,resetReason,pm100,gasPpm,tvoc,eco2,aqi,notas
```

No incluye columnas SHT31. La política de valores ausentes del CSV difiere de
la de HTTP: algunos campos se escriben como `0`, y los SHT4x pueden conservar
`nan` o el último valor. Sin RTC, el CSV usa fecha de respaldo `01_01_2000`;
verificar fecha/hora antes de una campaña.

Archivos auxiliares:

- `/errors_h<ID>.csv`: `timestamp,type,context,message`.
- `/failed_h<ID>.csv`: `timestamp,error_type,url`. Registra intentos fallidos,
  incluidos fallos de preparación de red; no es una cola de retransmisión.

La cabecera solo se crea si el archivo está vacío. Después de actualizar
firmware, comprobar que un CSV existente no conserve una cabecera antigua.
`sdnew` usa el mismo nombre diario; no garantiza un archivo vacío distinto.

## Formatos de telemetría

Ambos formatos usan una única constante `API_BASE` con la dirección
`http://api-sensores.cmasccp.cl/insertarMedicion`. La diferencia está en los
IDs, las variables y la cantidad de valores; no en el endpoint.

### Equipos 1 a 13

Usan `http://api-sensores.cmasccp.cl/insertarMedicion` y 17 valores.
Por ejemplo, para el equipo **3**, `getIdsSensores()` devuelve:

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

### Serie estática AE

Los nombres actuales son `1AE`, `2AE`, `3AE`, `4AE` y `5AE`.
`1AE` reemplaza al antiguo equipo 14; el ID numérico `14` se conserva
como alias compatible de ese mismo mapeo.

| Equipo | Plantower: T, HR, PM1, PM2.5, PM10 | SHT41: T, HR |
|---|---|---|
| 1AE (antes 14) | 1232 | 1233 |
| 2AE | 1234 | 1235 |
| 3AE | 1236 | 1237 |
| 4AE | 1238 | 1239 |
| 5AE | 1244 | 1245 |

Todos usan la misma `API_BASE`:
`http://api-sensores.cmasccp.cl/insertarMedicion`.
La nomenclatura AE cambia el mapeo, no crea otro endpoint.
Se conserva el protocolo HTTP configurado en la URL base compartida.

`idsSensores` repite el ID Plantower cinco veces y el SHT41 dos veces.
Las variables son siempre `3,6,7,8,9,3,6`; los siete valores se envían en este orden:

1. Temperatura Plantower (°C).
2. Humedad Plantower (%).
3. PM1 (µg/m³).
4. PM2.5 (µg/m³).
5. PM10 (µg/m³).
6. Temperatura SHT41 (°C).
7. Humedad SHT41 (%).

Ejemplo de `5AE`:

```text
idsSensores=1244,1244,1244,1244,1244,1245,1245
idsVariables=3,6,7,8,9,3,6
```

Se envían lecturas numéricas, sin descripciones ni unidades. SHT4x ausente
produce `-1` en sus dos posiciones, sin reemplazo SHT31. Los flotantes
no finitos se envían como `-1`; los PM pueden ser cero y no hay comprobación
universal de antigüedad de muestras.

La selección usa el nombre completo AE, no `toInt()`: `1AE` no debe
confundirse con el antiguo equipo `1`. Los equipos numéricos 1–13 conservan
sus mapeos de 17 valores. No se crean alias numéricos 15, 16, etc.
Un nombre no configurado, como `6AE`, o el error tipográfico `1EA`, no envía
y registra `HTTP_MAPPING_MISSING`.

Configurar `DEVICE_ID_STR = "1AE"` (o el AE correspondiente) antes de compilar.
El nombre se propaga a OLED, `HIRIPRO_1AE`, `/hiripro1AE_DD_MM_YYYY.csv`,
`/errors_h1AE.csv` y `/failed_h1AE.csv`. Los archivos anteriores no se renombran
ni migran automáticamente. La cabecera CSV conserva sus 27 columnas.

## WiFi SD

El menú Información → WIFI SD (ON/OFF) abre la pantalla de control; BTN2
activa/desactiva el AP. Se requiere SD montada y un BTN2 físico habilitado.
Desde el AP se pueden listar, descargar, subir, renombrar y borrar archivos.
El DNS cautivo dirige al gestor en `http://192.168.4.1`.

Mientras WiFi está activo, `loop()` atiende DNS/WebServer y retorna antes de
la adquisición normal, guardado periódico, HTTP y consola serial. Salir del
modo WiFi para reanudar esas tareas. Conservar copias de los datos antes de borrar.

## Compilar, cargar y verificar

Desde la raíz del proyecto:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 .
```

Reemplazar `COMx` por el puerto real. Dependencias y flujo de validación:
[Manual técnico](documentacion/MANUAL_TECNICO.md).

Pruebas de simulación (Python y `g++`):

```powershell
python tests/test_offline_logging.py
python tests/test_measurement_payload.py
```

Ejercitan conexión, pausa, servicio SD durante esperas y construcción de URLs.
No escriben mediciones en el servidor ni sustituyen una prueba física.
La compilación registrada de Pro V0.1.30R con core ESP32 2.0.17 fue correcta;
la recepción real del backend y el comportamiento eléctrico requieren validación en placa.

## Diagnóstico de conexión y reinicios

La señal CSQ no confirma acceso al servidor. Revisar `[NET]`, el resultado
HTTP y `[SD] Saved line` para distinguir conexión, envío y almacenamiento.
Al reiniciar se imprimen:

```text
[RESET] Reason: ...
[RESET] Previous stage before reset: ...
```

Un fallo de conexión no invoca un reinicio explícito. La sospecha de energía
reportada en operación no está confirmada por un registro de reset en este repo.
No atribuir un reinicio a HTTP solo porque el último mensaje sea `[HTTP] GET`.

## Documentación y módulos

- [Manual de usuario](documentacion/MANUAL_USUARIO.md): operación y comandos.
- [Manual técnico](documentacion/MANUAL_TECNICO.md): arquitectura, tiempos y límites.
- [Historial](documentacion/CAMBIOS.md): cambios por versión y revisiones documentales.
- [Guía de desarrollo](documentacion/DEVELOPER_GUIDELINES.md): mantenimiento y validación.
- [Menú OLED](documentacion/menu_structure.md): acciones y disponibilidad física.

El núcleo está en el sketch principal; `http.ino` administra red/HTTP,
`sd_card.ino` escribe CSV, `PMSandSensorEXTRA.ino` procesa PMS/SDS198,
`config.h/config.ino` definen pines y preferencias, `ui.ino` controla OLED,
`wifi.ino` gestiona archivos y `gps.ino/rtc.ino/helpers.ino/animacion.ino`
contienen GNSS, hora, utilidades y animación.

## Responsabilidad y licencia

Este proyecto se entrega **tal cual**, sin garantías de funcionamiento,
disponibilidad o aptitud para un propósito específico. La validación en terreno,
la calidad de datos y la integración eléctrica corresponden al usuario o integrador.

La documentación se publica bajo
[Creative Commons Attribution-NonCommercial 4.0 International](https://creativecommons.org/licenses/by-nc/4.0/):
se permite compartir y adaptar con atribución y sin uso comercial no autorizado.
Consultar también [LICENSE](LICENSE).

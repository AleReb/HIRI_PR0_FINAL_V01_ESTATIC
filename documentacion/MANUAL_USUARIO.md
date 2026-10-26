# FirmwarePro — Manual de usuario

Referencia: **Pro V0.1.29R**. Revisión: **2026-10-26**.
Configuración actual del código: **ID 3**, AP **HIRIPRO_3** y
APN **gigsky-02**. El ID se cambia en el sketch antes de compilar.

## 1. Qué registra el equipo

La estación lee sensores ambientales y escribe un CSV en SD. Puede enviar las
mediciones actuales a un servidor mediante SIM7600. Incluye OLED, notas de
eventos y un gestor WiFi para respaldar archivos.

El código contempla Plantower/PMS, SDS198, SHT4x/SHT41, SHT31, MultiGas y ENS160.
Un sensor no instalado puede aparecer como no detectado sin impedir el uso de
los otros. El formato de envío del equipo 14 incluye solo Plantower y SHT41.

## 2. Encendido y registro automático

1. Insertar la SD y energizar con una alimentación estable.
2. GPIO0 corta alimentación I²C 250 ms, la activa en HIGH y espera 700 ms
   antes de continuar. Permanece habilitado también sin red.
3. Revisar versión/ID en la animación y estados de OLED, SD, RTC y sensores.
4. El diagnóstico de arranque permanece unos 3 s.
5. Se habilita registro local antes de probar el módem.
6. En operación normal, la primera muestra se guarda a los 3 minutos desde
   que se habilitó el registro; después se repite el intervalo.
7. El equipo abre automáticamente la vista debug y programa un primer turno
   de conexión/envío inmediato.

Autostart, registro cada 3 minutos, turnos HTTP cada 5 minutos y auto debug
se fuerzan al arrancar. No hace falta SIM ni fix GPS para guardar localmente.
GNSS está apagado por defecto, salvo preferencia guardada en contrario.

## 3. Sin SIM, sin cobertura o sin Internet

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

La condición para guardar es **SD operativa y registro habilitado**.
Mantener la alimentación estable; estas protecciones de software no evitan
un reinicio causado por caída de tensión.

## 4. Botones y menú OLED

| Control | Acción |
|---|---|
| BTN1, GPIO39 | Navega/cancela según pantalla; requiere resistencia externa según placa |
| BTN1 en debug | Intenta envío manual y luego un guardado SD |
| BTN2 | Selecciona/confirma; sale de debug; alterna WiFi en su pantalla |
| GPIO0 | Enable de I²C; no es un botón |

**BTN2 está deshabilitado (`-1`) en esta compilación.** Las acciones que lo
necesitan no están disponibles hasta asignarle un GPIO y conectar el botón.
La salida normal de auto debug usa BTN2. No interpretar el listado del menú
como garantía de que todos sus controles físicos están disponibles.

Menú principal: PM2.5, TEMPERATURA, HUMEDAD, EMPEZAR/DETENER MUESTREO, OPCIONES.
Opciones contiene MENSAJES, CONFIGURACION e INFORMACION.

- Mensajes: CAMION, HUMO, CONSTRUCCION, OTROS, VOLVER.
- Configuración: RTC, REINICIAR, VOLVER.
- Información: VERSION, REDES, GPS, GUARDADO, WIFI SD (ON/OFF), MODO DEBUG, VOLVER.

Las notas se escriben una vez en `notas` y después se limpian.
La primera nota tras arrancar se inicializa como `9`.

Para iniciar/detener desde menú, abrir el prompt con BTN2: BTN1 cancela y BTN2
confirma. El inicio de menú intenta montar SD y habilita registro si funciona.
La acción rápida BTN1 en debug no inicia/detiene el muestreo.
Consultar [estructura completa](menu_structure.md).

## 5. Consola: iniciar y detener no son equivalentes al menú

Conectar monitor a **115200 baud**, fin de línea LF (`\n`).
Los comandos se convierten a minúsculas.

- `stats`: revisar `Streaming` y `SD logging`.
- `stop`: cuando está en marcha, desactiva transmisión y guardado periódico.
- `start`: si estaba detenido, inicia transmisión con **SD logging apagado**;
  no usarlo para reactivar el registro local.
- Para recuperar el registro automático después de `stop`, reiniciar el equipo
  (autostart lo activa) o usar el inicio del menú si BTN2 está disponible.
- BTN1 en debug intenta una escritura puntual en SD; no reactiva el registro
  periódico si estaba apagado.

Estas diferencias describen el firmware actual; no se modificaron en esta
revisión documental.

## 6. Datos de SD y respaldo

Nombre diario: `/hiripro<ID>_DD_MM_YYYY.csv`.
El equipo activo usa prefijo `hiripro3`.
Cabecera actual de **27 columnas**:

```csv
ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht4xTempC,sht4xHum,resetReason,pm100,gasPpm,tvoc,eco2,aqi,notas
```

No hay columnas SHT31 en el CSV actual. Los datos ausentes pueden aparecer
como cero, NaN o un valor anterior según el campo; no considerar todo cero
como una lectura válida sin revisar el estado del sensor.

Archivos adicionales:

- `/errors_h<ID>.csv`: errores de sensores, red y HTTP.
- `/failed_h<ID>.csv`: URLs de intentos fallidos, incluso sin llegar a transmitir.

Esos archivos sirven para diagnóstico; no se reenvían automáticamente al
regresar la conexión. Sin RTC, el CSV usa fecha de respaldo `01_01_2000`.
Verificar fecha/hora antes del uso en terreno.

El firmware no reemplaza la cabecera de un archivo que ya tiene contenido.
Después de una actualización, respaldar y comprobar que la cabecera corresponda
a los datos nuevos. `sdnew` genera el nombre diario y puede reabrir el mismo CSV.

## 7. Telemetría según el equipo

Los equipos 1 a 13 envían 17 valores a la URL HTTP anterior. El equipo 14
usa HTTPS y siete lecturas: temperatura/humedad Plantower, PM1, PM2.5, PM10
y temperatura/humedad SHT41.

Para el 14: sensores `1232,1232,1232,1232,1232,1233,1233`, variables
`3,6,7,8,9,3,6`. Se envían números, no los textos de unidades.
SHT41 ausente se indica con `-1` en sus dos posiciones.

Esta copia sigue configurada con **ID 3**; no se transforma en equipo 14
automáticamente. Para IDs posteriores al 14 se requieren sus mapeos reales.
El cambio de formato HTTP no modifica las 27 columnas de SD.
Detalles en [README](../README.md#formatos-de-telemetría).

## 8. WiFi SD

Desde Información → WIFI SD (ON/OFF), BTN2 activa/desactiva el AP.
Se requiere SD disponible y BTN2 habilitado físicamente.

```text
SSID: HIRIPRO_3
Contraseña: 12345678
Dirección: http://192.168.4.1
```

Permite listar, descargar, subir, renombrar y borrar archivos, incluido borrado
total. Respaldar los datos antes de borrar.

**Mientras WiFi SD está activo, el ciclo normal de medición, guardado SD,
envío HTTP y consola serial está pausado.** BTN1 no sale de la pantalla con
WiFi activo; usar BTN2 para apagarlo y luego BTN1 para salir.
El AP sirve archivos localmente; no proporciona Internet al módem.

## 9. Configuración y comandos disponibles

Cada `set` válido guarda automáticamente. Al reiniciar se vuelven a imponer
los periodos SD/HTTP, autostart, no esperar GPS y auto debug de la estación.
GNSS y otras preferencias conservan su valor guardado.

| Grupo | Comandos |
|---|---|
| Ayuda | `help`, `?` |
| Estado | `stats`, `counters`, `resetcnt`, `sysinfo`, `mem` |
| SD | `sdinfo`, `sdlist`, `sdnew`, `sdclear`, `sdclear confirm` |
| Red | `netinfo`, `csq` |
| Hora | `rtc`, `modemtime`, `rtcsync` |
| Control | `start`, `stop`, `reboot`, `i2c reset` |
| Configuración | `config`, `config sd`, `config http`, `config display`, `config power`, `config system` |
| Guardar/restablecer | `configsave`, `configreset` |

`sdclear` pide confirmación; `sdclear confirm` ejecuta el borrado.
`sdnew` no garantiza un nombre diferente al archivo del mismo día.

| Ajuste | Valores aceptados |
|---|---|
| `set sdauto` | `on/off`; el arranque de estación vuelve a activar SD |
| `set sdsave` | `3/60/600/1200`, en **segundos** |
| `set httpsend` | `3/60/600/1200`, en segundos |
| `set httptimeout` | Entero de 5 a 30 segundos |
| `set oledoff` | `on/off` |
| `set oledtime` | `60/120/180` segundos |
| `set led` | `on/off` |
| `set ledbright` | `10/25/50/100` % |
| `set autostart` | `on/off`; el arranque lo vuelve a activar |
| `set gnss` | `on/off`; reiniciar para aplicar inicio completo |
| `set autowaitgps` | `on/off`; el arranque lo fuerza a off |
| `set autogpsto` | Entero de 60 a 900 segundos; no hay espera GPS en este arranque |
| `set autodebug` | `on/off`; el arranque lo vuelve a activar |
| `set rotatedisplay` | `on/off` |
| `set reboot` | Entero de 0 a 720 horas; parámetro almacenado, temporizador no implementado en loop |
| `set gnssmode` | `1/3/5/7/15`; reiniciar para aplicar |

`set sdsave 3` guarda cada tres **segundos**, no minutos. Al reiniciar,
el periodo vuelve a 180 s. `configreset` guarda defaults y requiere reiniciar
para aplicar los flujos de arranque; la rotación por defecto de ese comando
difiere de la carga de preferencias vacías.

## 10. Diagnóstico

| Síntoma | Comprobación |
|---|---|
| No guarda | `sdinfo`, `stats`, registro habilitado, tarjeta y alimentación; salir de WiFi SD |
| No envía | SIM lista, APN `gigsky-02` apropiado para esa SIM, mensajes `[NET]`, pausa y resultado HTTP |
| Se ve `[HTTP] GET` sin recibir datos | Ese mensaje precede la comprobación de conexión; buscar resultado posterior |
| Buena señal pero servidor no responde | CSQ no confirma Internet; revisar errores HTTP y conexión de datos |
| No abre la web | Conectar a `HIRIPRO_3` y abrir manualmente `http://192.168.4.1` |
| No se puede salir de debug/entrar al gestor | Comprobar disponibilidad de BTN2; en esta configuración está deshabilitado |
| GPS sin datos | Apagado por defecto; `set gnss on` y reinicio si se necesita |
| Fecha incorrecta | Revisar `rtc`; `rtcsync` requiere hora válida del módem |
| Reinicia | Capturar motivo y etapa previa de reset, versión y últimas líneas serie; revisar alimentación según el resultado |

Los logs de reset son `[RESET] Reason: ...` y
`[RESET] Previous stage before reset: ...`.
El problema de energía reportado por el usuario es una hipótesis operativa,
no una causa confirmada por un log incluido en el repositorio.

## 11. Validación y licencia

La compilación y las pruebas simuladas de conexión/payload fueron aprobadas.
La simulación no reemplaza comprobar SD, alimentación y recepción del backend
con el dispositivo real. Para mantenimiento, consultar el
[Manual técnico](MANUAL_TECNICO.md) y el [historial](CAMBIOS.md).

El firmware y la documentación se entregan tal cual, sin garantías de desempeño.
Validar calibración y calidad de datos antes de usar las mediciones.
La contraseña AP es conocida; gestionar archivos en un entorno controlado.

Documentación bajo [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/).
Consultar [LICENSE](../LICENSE).

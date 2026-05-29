# FirmwarePro - Manual de Usuario

Versión de referencia: **Pro V0.1.27R**  
Plataforma: **ESP32 Dev Module (`esp32:esp32:esp32`)**  
Sketch principal: **`HIRI_PR0_FINAL_V01_ESTATIC.ino`**

---

## 1. Propósito del equipo

FirmwarePro es un firmware para monitoreo ambiental y telemetría técnica que integra:

- Sensor **PMS5003** para PM1.0, PM2.5, PM10 y T/H cuando el módulo lo entrega.
- Sensor **SDS198** por `Serial2` para PM100/TSP.
- Sensor **SHT4x** para temperatura/humedad, con soporte para **SHT31**.
- Sensor **DFRobot MultiGas I2C** para concentración de gas.
- Sensor **ENS160** para TVOC, eCO2 y AQI.
- Módem **SIM7600** para red celular, HTTP y soporte GNSS configurable.
- Registro en **tarjeta SD** en formato CSV diario.
- Envío HTTP a backend remoto.
- Interfaz local por **OLED + botones**.
- Modo **WiFi AP** para gestión de archivos en SD desde celular o PC.

La configuración activa corresponde a una variante de estación HIRIDUST/VALPO con `DEVICE_ID_STR = "12"`.

---

## 2. Arranque rápido

1. Energiza el equipo.
2. El firmware activa GPIO0 como enable de alimentación I2C: primero LOW, luego HIGH para encender OLED y sensores.
3. Espera la animación de inicio. En la esquina superior derecha se muestra la versión y abajo a la derecha el ID del dispositivo.
4. Revisa la ventana única de diagnóstico de arranque. Cada fila pasa a `OK` o `FAIL`:
   - I2C
   - OLED
   - SD
   - RTC
   - SHT4
   - SHT31
   - ENS
   - GAS
5. La ventana queda visible cerca de 3 segundos antes de continuar con módem/red y pantallas normales.
6. La estación usa autostart por defecto: inicia guardado SD y transmisión HTTP automáticamente si la configuración no fue modificada.
7. En pantalla usa:
   - **BTN1:** navegar, cancelar prompt o acción rápida según pantalla.
   - **BTN2:** seleccionar, confirmar o salir según pantalla.
8. Para iniciar o detener manualmente:
   - Menú principal -> **EMPEZAR/DETENER MUESTREO** -> confirmar con BTN2.

Notas de hardware:

- En la configuración actual, **BTN1 está en GPIO39**.
- **BTN2 está deshabilitado por defecto** en `config.h` (`BUTTON_PIN_2 = -1`). Si el hardware necesita BTN2, se debe asignar un GPIO real.
- **GPIO0 no es botón**: se usa como enable de alimentación I2C (`I2C_POWER_PIN`) para encender y reiniciar OLED/sensores.

---

## 3. Navegación por pantalla OLED

### Menú principal

- PM2.5
- Temperatura
- Humedad
- Empezar/Detener muestreo
- Opciones

### Opciones

- Mensajes
- Configuración
- Información
- Volver

### Mensajes

Las opciones guardan una nota one-shot en la próxima fila CSV:

- Camion
- Humo
- Construccion
- Otros
- Volver

La nota se escribe en la columna `notas` y luego se limpia automáticamente para no repetirse.

### Configuración

- RTC
- Reiniciar
- Volver

### Información

- Versión
- Redes
- GPS
- Guardado
- WiFi SD (ON/OFF)
- Modo Debug
- Volver

---

## 4. Modo WiFi SD

Al activar **WIFI SD (ON/OFF)**, el equipo levanta un AP WiFi:

```text
SSID: HIRIPRO_12
Password: 12345678
IP típica: 192.168.4.1
```

El gestor web permite:

- Ver archivos de la SD.
- Descargar archivos.
- Subir archivos.
- Renombrar archivos.
- Borrar archivos.
- Borrar todo.

En modo WiFi el firmware prioriza DNS y WebServer para mejorar la estabilidad de transferencia. También incluye captive portal DNS para facilitar el acceso desde Windows.

---

## 5. Indicadores de operación

La pantalla OLED muestra, según el modo:

- Animación de arranque con versión e ID.
- Ventana de revisión de inicio con `I2C`, `OLED`, `SD`, `RTC`, `SHT4`, `SHT31`, `ENS` y `GAS`.
- Hora o estado de operación.
- Actividad y último estado de SD/HTTP.
- Datos de sensores.
- Red celular y CSQ.
- Batería.
- Estado de archivo SD.
- Estado GPS si GNSS está habilitado.

El LED NeoPixel queda habilitado por defecto al 50 % y se usa para estados de arranque, módem y nivel PM2.5.

---

## 6. Datos registrados en SD

Archivo diario por dispositivo:

```text
/hiripro<ID>_DD_MM_YYYY.csv
```

Con `DEVICE_ID_STR = "12"`, el nombre queda con prefijo `hiripro12`.

Cabecera CSV actual:

```text
ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,sht31Hum,sht4xTempC,sht4xHum,resetReason,pm100,gasPpm,tvoc,eco2,aqi,notas
```

Archivos auxiliares:

- `/errors_h<ID>.csv`: errores estructurados con `timestamp,type,context,message`.
- `/failed_h<ID>.csv`: transmisiones HTTP fallidas con `timestamp,error_type,url`.

---

## 7. Configuración por defecto de estación

Valores definidos en `config.ino`:

```text
Guardado SD: cada 180 s (3 min)
Envío HTTP: cada 300 s (5 min)
Timeout HTTP: 15 s
OLED auto-off: desactivado
LED NeoPixel: habilitado al 50 %
Autostart: activado
Esperar GPS antes de iniciar: desactivado
Auto debug: activado
GNSS: desactivado por defecto
Modo GNSS si se habilita: 15
```

---

## 8. Uso recomendado en terreno

1. Verificar batería, SD y cobertura celular antes de campaña.
2. Confirmar que el equipo arranca sin errores críticos.
3. Revisar que se cree el CSV diario.
4. Confirmar transmisión HTTP si hay red.
5. Usar notas de eventos cuando corresponda.
6. Respaldar la SD por WiFi SD o extrayendo la tarjeta.

Si se requiere posición GNSS, habilitar GNSS por comando serial y validar fix antes de usar esos datos como referencia.

---

## 9. Solución de problemas

### No aparece la web en Windows

- Confirmar conexión al SSID `HIRIPRO_12`.
- Abrir manualmente `http://192.168.4.1`.
- Desconectar y reconectar WiFi del PC.

### No guarda en SD

- Revisar que la SD esté bien insertada.
- Verificar estado SD en pantalla o por comando serial `sdinfo`.
- Confirmar que `loggingEnabled` esté activo usando `start` o autostart.

### No transmite HTTP

- Revisar CSQ con `csq`.
- Revisar operador y registro con `netinfo`.
- Confirmar cobertura celular y APN `flolive.net`.
- Revisar `/failed_h<ID>.csv`.

### GPS sin datos

- En esta configuración GNSS está apagado por defecto.
- Habilitar con `set gnss on` y guardar con `configsave`.
- Dar tiempo a fix GNSS y revisar antena/vista al cielo.

---

## 10. Comandos seriales

Baudrate: **115200**, fin de línea `\n`.

Comandos principales:

- `help` / `?`
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
- `set httptimeout <5..30>`
- `set oledoff on|off`
- `set oledtime 60|120|180`
- `set led on|off`
- `set ledbright 10|25|50|100`
- `set autostart on|off`
- `set gnss on|off`
- `set autowaitgps on|off`
- `set autogpsto <60..900>`
- `set autodebug on|off`
- `set rotatedisplay on|off`
- `set gnssmode 1|3|5|7|15`
- `configreset`, `configsave`

---

## 11. Seguridad operativa

- La password AP por defecto es conocida (`12345678`); usar en entorno controlado.
- Evitar exposición pública del AP.
- Respaldar la SD periódicamente.
- Validar calibración y calidad de datos antes de usar mediciones para decisiones formales.

---

## 12. Disclaimer de responsabilidad

Este firmware y su documentación se entregan **"tal cual"**, sin garantías explícitas ni implícitas de desempeño, disponibilidad o aptitud para un propósito particular.

El uso en terreno, decisiones operativas y cumplimiento normativo son responsabilidad del usuario o institución que lo despliega.

El autor y colaboradores no se responsabilizan por pérdidas de datos, daños directos o indirectos, ni por usos fuera de contexto técnico seguro.

---

## 13. Licencia

Este manual se publica bajo licencia:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

En resumen:

- Puedes compartir y adaptar con atribución.
- No se permite uso comercial sin autorización adicional.

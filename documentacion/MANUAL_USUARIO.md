# FirmwarePro — Manual de Usuario

Versión de referencia: **Pro V0.0.35**  
Plataforma: **ESP32 Dev Module (esp32:esp32:esp32)**

---

## 1) Propósito del equipo

FirmwarePro es un firmware para monitoreo ambiental y telemetría técnica que integra:

- Sensor de material particulado **PMS5003** (PM1/PM2.5/PM10 + T/H del módulo ST cuando aplica)
- Sensor **SDS198** (PM100)
- Sensor **SHT31** (temperatura/humedad, opcional)
- **GNSS** por módem SIM7600 (posición, altitud, velocidad, satélites, HDOP)
- Registro en **tarjeta SD** (CSV)
- Envío HTTP a backend remoto
- Interfaz local por **OLED + 2 botones**
- Modo **WiFi AP** para gestión de archivos en SD desde celular/PC

---

## 2) Arranque rápido

1. Energiza el equipo.
2. Espera animación de inicio y estado de módem/red.
3. En pantalla principal usa:
   - **BTN1**: navegar
   - **BTN2**: seleccionar/confirmar
4. Para empezar muestreo/transmisión:
   - Menú principal → **EMPEZAR MUESTREO** → confirmar con BTN2.

---

## 3) Navegación por pantalla (OLED)

## Menú principal
- PM2.5
- Temperatura
- Humedad
- Empezar/Detener muestreo
- Opciones

## Opciones
- Mensajes
- Configuración
- Información
- Volver

## Mensajes (nota one-shot al CSV)
- Camion
- Humo
- Construccion
- Otros
- Volver

> La nota seleccionada se escribe en la próxima fila CSV (columna `notas`) y luego se limpia automáticamente.

## Configuración
- RTC
- Reiniciar
- Volver

## Información
- Versión
- Redes
- GPS
- Guardado (archivo actual y tamaño)
- WIFI SD (ON/OFF)
- Modo Full
- Volver

---

## 4) Modo WiFi SD (transferencia de archivos)

Al activar **WIFI SD (ON/OFF)**:

- El equipo levanta un AP WiFi con:
  - SSID: `HIRIPRO_<ID>` (ejemplo `HIRIPRO_1`)
  - Password: `12345678`
  - IP típica: `192.168.4.1`
- Se habilita servidor web para:
  - Ver archivos SD
  - Descargar
  - Subir
  - Renombrar
  - Borrar
  - Borrar todo

### Prioridad transferencia
En modo WiFi, el firmware prioriza atención de DNS/Web para mejorar estabilidad de transferencia.

### Compatibilidad Windows
Se incluye **DNS tipo captive portal** para mejorar detección en Windows y facilitar apertura del gestor web.

### Salida
Desde la UI, al salir del modo WiFi se apaga el AP y el equipo vuelve al ciclo normal.

---

## 5) Indicadores de operación

- Header OLED:
  - Hora
  - Estado de transmisión/SD (actividad y último estado)
  - Satélites/fix
  - Señal de red (CSQ)
  - Batería
- LED RGB (NeoPixel): nivel PM2.5 por gradiente de color.

---

## 6) Datos registrados en SD

Archivo diario por dispositivo:

`/hiripro<ID>_DD_MM_YYYY.csv`

Cabecera CSV:

`ts_ms,time,gpsDate,lat,lon,alt,spd_kmh,pm1,pm25,pm10,pmsTempC,pmsHum,rtcTempC,batV,csq,sats,hdop,xtra_ok,sht31TempC,sht31Hum,resetReason,pm100,notas`

Además:
- Log de errores: `/errors_h<ID>.csv`
- Transmisiones fallidas: `/failed_h<ID>.csv`

---

## 7) Uso recomendado en terreno

1. Verifica batería y CSQ antes de campaña.
2. Espera fix GNSS estable (satélites/HDOP razonable).
3. Inicia muestreo.
4. Usa notas (Camion/Humo/Construccion/Otros) para eventos de contexto.
5. Al finalizar, usa WiFi SD para respaldo rápido de CSV.

---

## 8) Solución de problemas (usuario)

### No aparece la web en Windows
- Confirmar conexión al SSID HIRIPRO.
- Abrir manualmente `http://192.168.4.1`.
- Desconectar/reconectar WiFi del PC.

### No guarda en SD
- Revisar que la SD esté bien insertada.
- Verificar en menú/serial estado SD.

### No transmite HTTP
- Revisar CSQ y operador.
- Confirmar cobertura celular y APN.

### GPS sin fix
- Dar más tiempo al GNSS al inicio.
- Revisar antena y vista al cielo.

---

## 9) Apéndice A — Comandos seriales (usuario avanzado)

Baudrate: **115200**, fin de línea `\n`.

Comandos principales:

- `help` / `?`
- `rtc`
- `counters`, `resetcnt`, `stats`
- `sdinfo`, `sdlist`, `sdnew`, `sdclear`, `sdclear confirm`
- `netinfo`, `csq`
- `sysinfo`, `mem`, `reboot`
- `start`, `stop`
- `config`, `config sd/http/display/power`
- `set sdauto on/off`
- `set sdsave 3|60|600|1200`
- `set httpsend 3|60|600|1200`
- `set httptimeout <5..30>`
- `set oledoff on/off`
- `set oledtime 60|120|180`
- `set led on/off`
- `set ledbright 10|25|50|100`
- `set autostart on/off`
- `set autowaitgps on/off`
- `set autogpsto <60..900>`
- `set gnssmode 1|3|5|7|15`
- `configreset`, `configsave`

---

## 10) Apéndice B — Seguridad operativa

- La password AP por defecto es conocida (`12345678`), usar en entorno controlado.
- Evitar exposición pública del AP.
- Respaldar SD periódicamente.

---

## 11) Disclaimer de responsabilidad

Este firmware y su documentación se entregan **"tal cual"**, sin garantías explícitas ni implícitas de desempeño, disponibilidad o aptitud para un propósito particular.  
El uso en terreno, decisiones operativas y cumplimiento normativo son responsabilidad del usuario/institución que lo despliega.  
El autor y colaboradores no se responsabilizan por pérdidas de datos, daños directos o indirectos, ni por usos fuera de contexto técnico seguro.

---

## 12) Licencia

Este manual se publica bajo licencia:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

En resumen:
- ✅ Puedes compartir y adaptar con atribución.
- ❌ No se permite uso comercial sin autorización adicional.

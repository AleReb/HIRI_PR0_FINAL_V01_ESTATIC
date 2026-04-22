# FirmwarePro (HIRI)

Firmware para estación de monitoreo técnico-científico basada en ESP32, con adquisición de sensores, GNSS, registro en SD, telemetría HTTP y gestor de archivos por WiFi AP.

## Estado

- Plataforma objetivo: **ESP32 Dev Module** (`esp32:esp32:esp32`)
- Versión de referencia documentada: **Pro V0.0.35**

## Funcionalidades principales

- Lectura de **PMS5003** (PM1/PM2.5/PM10 + T/H según modelo)
- Lectura de **SDS198** (PM100)
- Lectura opcional de **SHT31**
- GNSS por módem SIM7600 (posición, altitud, velocidad, satélites, HDOP)
- Registro en **CSV diario** en tarjeta SD
- Envío HTTP de mediciones a backend
- UI local con OLED + 2 botones
- Modo **WiFi SD** con web manager (upload/download/rename/delete)
- Captive portal DNS para mejor compatibilidad en Windows

## Documentación

- **Manual de Usuario:** `documentacion/MANUAL_USUARIO.md`
- **Manual Técnico:** `documentacion/MANUAL_TECNICO.md`
- **Historial de cambios:** `documentacion/CAMBIOS.md`
- **Guía de desarrollo/versionado:** `documentacion/DEVELOPER_GUIDELINES.md`
- **Estructura de menú:** `documentacion/menu_structure.md`
- **Licencia:** `LICENSE`

## Compilación y carga (Arduino CLI)

### Compilar

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 FirmwarePro.ino
```

### Subir a dispositivo (ejemplo COM5)

```bash
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32 FirmwarePro.ino
```

## Mapeo de Sensores HTTP
El envio HTTP usa `DEVICE_ID_STR` para seleccionar una lista fija de `idsSensores` en `getIdsSensores()`.
La lista de `idsVariables` es comun para todos los dispositivos soportados:

```text
53,54,55,11,12,15,45,46,4,3,6,7,8,9,51,3,6
```

El payload `valores` se arma en ese mismo orden: SO2/gas, TVOC, eCO2, latitud,
longitud, CSQ, velocidad, satelites, bateria, temperatura PMS, humedad PMS,
PM1.0, PM2.5, PM10, PM100 SDS198, temperatura SHT y humedad SHT.

Los datos faltantes o invalidos se transmiten como `-0`.

## Disclaimer de responsabilidad

Este proyecto se entrega **"tal cual"** (as-is), sin garantías explícitas ni implícitas de funcionamiento, disponibilidad o aptitud para un propósito específico.

El despliegue en terreno, la seguridad del sistema, la validación de datos y el cumplimiento normativo son responsabilidad del usuario/integrador.

Los autores y colaboradores no se responsabilizan por pérdidas de datos, daños directos o indirectos ni por uso fuera de contexto técnico seguro.

## Licencia de documentación

La documentación de este repositorio se publica bajo:

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**  
https://creativecommons.org/licenses/by-nc/4.0/

- ✅ Se permite compartir y adaptar con atribución.
- ❌ No se permite uso comercial sin autorización adicional.

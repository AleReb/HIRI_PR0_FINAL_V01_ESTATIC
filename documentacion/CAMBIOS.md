# Registro de Cambios - FirmwarePro

## [V0.1.0] - 2026-02-14
### Primera versión formal (baseline de producto)
- Se establece versión de firmware en código a **Pro V0.1.0** (`FirmwarePro.ino`).
- Ajustes finales de UI WiFi:
  - Pantalla WiFi dedicada mostrando **SSID / PASS / IP**.
  - Control por botones según estado WiFi:
    - Con WiFi activo: pantalla bloqueada (sin salida accidental).
    - Toggle ON/OFF por BTN2.
- Se repone feedback visual de arranque de módem:
  - Parpadeo de LED RGB durante `MODEM Starting...` para indicar actividad y evitar falsa percepción de bloqueo.
- Documentación ordenada en carpeta `documentacion/` con `README.md` en raíz y `LICENSE` en root del repo.

## [V0.0.35] - 2026-02-14
### WiFi SD, documentación y control de release
- Se consolidó versión de firmware a **Pro V0.0.35**.
- Se implementó flujo de **WiFi SD** con mejoras para uso desde Windows (captive portal DNS).
- Se creó documentación formal en Markdown:
  - `README.md`
  - `MANUAL_USUARIO.md`
  - `MANUAL_TECNICO.md`
  - `LICENSE` (CC BY-NC 4.0)
- Se realizó recuperación/validación de estabilidad tras pruebas (bootloop por SW reset reportado en terreno).
- **Commit con funcionamiento confirmado en equipo (booteo + flasheo exitoso):**
  - `6c66dbb` (reaplicación de cambios WiFi exclusivo)
  - Flasheado exitosamente en COM5 con verificación hash y reset OK.

## [V0.0.33] - 2026-02-14
### Notas operativas en CSV + menú Mensajes
- Se agregó nueva columna `notas` al CSV al final del header:
  - `...,pm100,notas`
- Se agregó opción **"Otros"** al submenú **Mensajes**.
- Lógica de nota one-shot:
  - Al seleccionar `Camion`, `Humo`, `Construccion` u `Otros`, se guarda esa nota en la próxima fila CSV.
  - Luego se limpia automáticamente para evitar repetición.

## [V0.0.26] - 2026-02-13
### Mejoras en UI y Lógica de Muestreo
- **Nuevo Prompt de Confirmación:** Se reemplazó el aviso simple por un cuadro de diálogo claro: "¿CONFIRMAR ACCIÓN? INICIAR/DETENER MUESTREO".
- **Lógica de Botones en Prompt:** 
  - **BTN1:** Cancela la acción y vuelve al menú.
  - **BTN2:** Confirma e inicia/detiene el proceso.
- **Muestreo Inmediato:** Al confirmar el inicio, se fuerza una ejecución inmediata de guardado en SD y envío HTTP para feedback visual instantáneo (LED/Header).
- **Depuración:** Se añadieron mensajes por puerto serial para rastrear clics de botones físicamente.

## [V0.0.25] - 2026-02-13
### Reestructuración de Navegación
- **Modo FULL en Menú:** Se eliminó el acceso por pulsación larga (Hold) y se añadió como la 5ª opción del Menú Principal.
- **Salida de Modo FULL:** Ahora se sale del modo bloqueado haciendo clic en el **BTN1 (izquierdo)**, devolviendo al usuario al menú principal.
- **Hold Desactivado:** Se deshabilitó la función de mantener presionado el botón derecho para evitar saltos accidentales de pantalla.

## [V0.0.24] - 2026-02-13
### Seguridad de Operación
- **Implementación de Full Lock:** Bloqueo de navegación de menús mientras el dispositivo está en modo de visualización de datos (Modo FULL).
- **Control de Muestreo:** Sincronización de BTN2 para alternar inicio/parada tanto en menú como en vista bloqueada.

## [V0.0.23] - 2026-02-13
### Versión Inicial Fusionada
- Integración de Backend GPSDebug con UI de Menús HIRI_PR0.
- Soporte para sensores PMS5003, SHT31, SDS198.
- Registro en SD y transmisión HTTP concurrente.

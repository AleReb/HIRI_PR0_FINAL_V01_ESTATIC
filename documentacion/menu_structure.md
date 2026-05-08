# Estructura del Menú FirmwarePro

Referencia: `ui.ino` en estado actual.

---

## Controles

Lógica de UI:

- **BTN1:** navegar/ciclar opciones, cancelar prompt o acción rápida en modo debug.
- **BTN2:** seleccionar, entrar, confirmar, salir de modo debug o alternar WiFi SD.

Hardware actual:

- `BUTTON_PIN_1 = 39`
- `BUTTON_PIN_2 = -1`

Con `BUTTON_PIN_2 = -1`, BTN2 queda deshabilitado por defecto y las acciones que dependen de BTN2 requieren asignar un GPIO real en `config.h`.

La lógica por hold fue retirada; el flujo actual es por clics.

---

## Estados especiales de UI

- `DISP_NORMAL`: navegación normal.
- `DISP_MESSAGE`: mensaje temporal no bloqueante.
- `DISP_PROMPT`: confirmación para iniciar/detener muestreo.
- `DISP_NETWORK`: pantalla de red.
- `DISP_RTC`: pantalla RTC.
- `DISP_STORAGE`: pantalla de archivo/tamaño.
- `DISP_GPS`: pantalla de estado GPS.
- `DISP_WIFI`: pantalla dedicada WiFi SD.
- `uiFullMode=true`: vista completa/modo debug de telemetría.
- `wifiModeActive=true`: AP/WebServer activos.

---

## 1. Menú principal

| Índice | Texto | Acción |
|---|---|---|
| 0 | PM2.5 | Vista de valor grande PM2.5 |
| 1 | TEMPERATURA | Vista de valor grande temperatura |
| 2 | HUMEDAD | Vista de valor grande humedad |
| 3 | EMPEZAR/DETENER MUESTREO | Abre `DISP_PROMPT` para confirmar |
| 4 | OPCIONES | Entra a submenú Opciones |

---

## 2. Submenú Opciones

| Índice | Texto | Acción |
|---|---|---|
| 0 | MENSAJES | Entra a Mensajes |
| 1 | CONFIGURACION | Entra a Configuración |
| 2 | INFORMACION | Entra a Información |
| 3 | VOLVER | Regresa al menú principal |

---

## 3. Submenú Mensajes

| Índice | Texto | Acción |
|---|---|---|
| 0 | CAMION | `currentNote = "Camion"` para próxima fila CSV |
| 1 | HUMO | `currentNote = "Humo"` para próxima fila CSV |
| 2 | CONSTRUCCION | `currentNote = "Construccion"` para próxima fila CSV |
| 3 | OTROS | `currentNote = "Otros"` para próxima fila CSV |
| 4 | VOLVER | Regresa a Opciones |

La nota es one-shot: se escribe una vez en la columna `notas` y luego se limpia.

---

## 4. Submenú Configuración

| Índice | Texto | Acción |
|---|---|---|
| 0 | RTC | Abre `DISP_RTC` |
| 1 | REINICIAR | Ejecuta reinicio por UI |
| 2 | VOLVER | Regresa a Opciones |

---

## 5. Submenú Información

| Índice | Texto | Acción |
|---|---|---|
| 0 | VERSION | Muestra `VERSION` |
| 1 | REDES | Abre `DISP_NETWORK` |
| 2 | GPS | Abre `DISP_GPS` |
| 3 | GUARDADO | Abre `DISP_STORAGE` |
| 4 | WIFI SD (ON/OFF) | Entra a pantalla WiFi SD |
| 5 | MODO DEBUG | Activa `uiFullMode` |
| 6 | VOLVER | Regresa a Opciones |

---

## Reglas de interacción

En `DISP_PROMPT`:

- BTN1 cancela.
- BTN2 confirma start/stop.

En `DISP_RTC`:

- BTN2 sincroniza RTC desde módem.

En modo debug/full:

- BTN1 ejecuta acción rápida relacionada con muestreo/transmisión.
- BTN2 sale al menú principal.

En pantalla WiFi:

- BTN1 no sale si WiFi está activo.
- BTN2 alterna WiFi SD ON/OFF.

---

## Checklist de mantenimiento

Cada modificación de menú debe sincronizarse con:

1. `ui.ino`
2. `documentacion/MANUAL_USUARIO.md`
3. `documentacion/menu_structure.md`
4. `documentacion/CAMBIOS.md` si es cambio funcional

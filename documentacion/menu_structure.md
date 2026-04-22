# Estructura del Menú FirmwarePro (Actualizada)

Referencia: `ui.ino` (estado actual de navegación y acciones).

## Controles

- **BTN1 (PIN 19):** navegar/ciclar opciones (`menuIndex + 1`).
- **BTN2 (PIN 23):** seleccionar / entrar / confirmar.

> Nota: la lógica por **hold** fue retirada. El flujo es por clics.

---

## Estados especiales de UI

- `DISP_NORMAL` → navegación normal.
- `DISP_MESSAGE` → mensaje temporal no bloqueante.
- `DISP_PROMPT` → confirmación de iniciar/detener muestreo.
- `DISP_NETWORK` → pantalla de red.
- `DISP_RTC` → pantalla RTC (con sincronización por BTN2).
- `DISP_STORAGE` → pantalla de archivo/tamaño.
- `DISP_GPS` → pantalla de estado GPS.
- **Modo FULL** (`uiFullMode=true`) → vista completa de telemetría.
- **Modo WiFi activo** (`wifiModeActive=true`) → pantalla dedicada WiFi y salida por BTN2.

---

## Estructura jerárquica actual

## 1) Menú Principal (Depth 0)

| Índice | Texto | Acción |
|---|---|---|
| 0 | PM2.5 | Vista de valor grande PM2.5 |
| 1 | TEMPERATURA | Vista de valor grande temperatura |
| 2 | HUMEDAD | Vista de valor grande humedad |
| 3 | EMPEZAR/DETENER MUESTREO | Abre `DISP_PROMPT` para confirmar |
| 4 | OPCIONES | Entra a submenú Depth 1 |

---

## 2) Submenú Opciones (Depth 1)

| Índice | Texto | Acción |
|---|---|---|
| 0 | MENSAJES | Entra Depth 2 |
| 1 | CONFIGURACION | Entra Depth 3 |
| 2 | INFORMACION | Entra Depth 4 |
| 3 | VOLVER | Regresa a Depth 0 |

---

## 3) Submenú Mensajes (Depth 2)

| Índice | Texto | Acción |
|---|---|---|
| 0 | CAMION | Set `currentNote="Camion"` (one-shot CSV) |
| 1 | HUMO | Set `currentNote="Humo"` (one-shot CSV) |
| 2 | CONSTRUCCION | Set `currentNote="Construccion"` (one-shot CSV) |
| 3 | OTROS | Set `currentNote="Otros"` (one-shot CSV) |
| 4 | VOLVER | Regresa a Depth 1 |

---

## 4) Submenú Configuración (Depth 3)

| Índice | Texto | Acción |
|---|---|---|
| 0 | RTC | Abre pantalla `DISP_RTC` |
| 1 | REINICIAR | Ejecuta `handleRestart()` |
| 2 | VOLVER | Regresa a Depth 1 |

---

## 5) Submenú Información (Depth 4)

| Índice | Texto | Acción |
|---|---|---|
| 0 | VERSION | Muestra versión (`DISP_MESSAGE`) |
| 1 | REDES | Abre `DISP_NETWORK` |
| 2 | GPS | Abre `DISP_GPS` |
| 3 | GUARDADO | Abre `DISP_STORAGE` |
| 4 | WIFI SD (ON/OFF) | Inicia/detiene AP WiFi |
| 5 | MODO FULL | Activa vista completa de telemetría |
| 6 | VOLVER | Regresa a Depth 1 |

---

## Reglas de interacción relevantes

- En `DISP_PROMPT`:
  - BTN1: cancelar.
  - BTN2: confirmar start/stop.
- En `DISP_RTC`:
  - BTN2: sincroniza RTC desde módem.
- En Modo FULL:
  - BTN1: toggle rápido muestreo.
  - BTN2: salir a menú normal.
- En modo WiFi activo:
  - BTN2: salir y apagar WiFi.

---

## Observación para desarrollo

Cada modificación de menú debe sincronizarse con:
1. `ui.ino`
2. `MANUAL_USUARIO.md`
3. `menu_structure.md`
4. `CAMBIOS.md` (si es cambio funcional)

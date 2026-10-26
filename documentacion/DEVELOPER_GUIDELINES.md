# Developer Guidelines - FirmwarePro

Este archivo define el flujo mínimo para mantener trazabilidad y control de versiones en este proyecto.

---

## 1. Regla de versión

Cada cambio funcional del firmware debe ir acompañado de incremento de versión.
Una corrección exclusivamente documental conserva la versión del firmware y
se registra como revisión documental con fecha en `CAMBIOS.md`.

- Ubicación actual: variable `VERSION` en `HIRI_PR0_FINAL_V01_ESTATIC.ino`.
- Versión actual documentada: **Pro V0.1.29R**.
- Formato recomendado: `Pro Vx.y.z` o el formato de release usado por el equipo.
- No mezclar múltiples features grandes sin subir versión.

---

## 2. Regla de changelog

Cada cambio funcional debe anotarse en `documentacion/CAMBIOS.md` en la misma sesión de trabajo.

Debe incluir:

- Número de versión.
- Fecha.
- Qué cambió.
- Riesgo o impacto.
- Commit relacionado cuando exista.

Si no se actualiza `CAMBIOS.md`, el cambio se considera incompleto.

---

## 3. Documentación sincronizada

Cuando cambie cualquiera de estos elementos, actualizar documentación:

- Pines o variante de hardware: `README.md`, `MANUAL_TECNICO.md`, `MANUAL_USUARIO.md`.
- Menú OLED o botones: `menu_structure.md` y `MANUAL_USUARIO.md`.
- CSV o archivos SD: `README.md`, `MANUAL_TECNICO.md`, `MANUAL_USUARIO.md`.
- Payload HTTP o `idsSensores`: `README.md`, `MANUAL_TECNICO.md`.
- Comandos seriales: `MANUAL_USUARIO.md`, `MANUAL_TECNICO.md`.
- Defaults de configuración: `README.md`, `MANUAL_TECNICO.md`.

---

## 4. Commit de funcionamiento confirmado

Cuando una versión sea validada en hardware real, registrar:

- Commit SHA.
- Resultado de prueba.
- Puerto/dispositivo de prueba si aplica.
- Observaciones de boot, SD, red, HTTP, WiFi y sensores.

Registrar en:

- `documentacion/CAMBIOS.md`
- release notes si existen

---

## 5. Flujo recomendado por cambio

1. Editar código.
2. Subir `VERSION` en `HIRI_PR0_FINAL_V01_ESTATIC.ino`.
3. Actualizar `documentacion/CAMBIOS.md`.
4. Actualizar documentación afectada.
5. Compilar con Arduino CLI.
6. Flashear en hardware de prueba.
7. Validar comportamiento crítico.
8. Revisar el diff y publicar commit/push cuando corresponda al alcance autorizado.
9. Registrar commit funcional confirmado.

---

## 6. Comandos estándar

Ejecutar desde la raíz del proyecto.

### Compilar

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
```

### Flashear

Cambiar `COMx` por el puerto real.

```bash
arduino-cli upload -p COMx --fqbn esp32:esp32:esp32 .
```

---

## 7. Criterios de listo para producción

Antes de marcar una versión como estable:

- Boot estable, sin loop de reset.
- OLED muestra versión e ID correctos.
- SD monta, crea CSV y escribe filas.
- CSV coincide con el header documentado.
- Sensores principales entregan valores o fallan con logs controlados.
- Módem registra red y obtiene CSQ.
- Sin SIM lista o sin conexión de datos, no se alcanza `HTTPACTION=0`.
- Tras tres fallos de conexión hay pausa de una hora y el registro SD continúa
  durante las esperas y la pausa, en operación normal.
- La recuperación respeta el siguiente turno HTTP; no exigir precisión al segundo.
- Si hay conexión celular pero falla el servidor, documentar que no se aplica
  la pausa de una hora por ese motivo.
- HTTP transmite o deja trazabilidad en `failed_h<ID>.csv`.
- Verificar los formatos de equipos 1–13 y del equipo 14, sin usar IDs inventados.
- WiFi SD operativo si aplica.
- GNSS validado si está habilitado para el despliegue.
- Documentación y changelog actualizados.

---

## 8. Auditoría de documentación contra el código

Antes de declarar documentación sincronizada, comprobar:

- `VERSION`, `DEVICE_ID_STR`, APN y URLs literales del sketch actual. El ID es
  específico de esta compilación y no debe copiarse de un historial antiguo.
- Cabecera exacta de `writeCSVHeader()` y posiciones de los datos; actualmente
  son 27 columnas y no hay columnas SHT31.
- `loadConfig()`, `configSetDefaults()` y `enforceStationStartupConfig()`:
  distinguir defaults, preferencias guardadas y valores impuestos al arrancar.
- Orden del constructor SoftwareSerial, no solo nombres de macros de pines.
- Condiciones `streaming`, `loggingEnabled` y retorno temprano de modo WiFi.
- Diferencia entre `start` serial, inicio de menú y autostart.
- Funciones definidas frente a funciones realmente llamadas: no anunciar
  temporizador de reinicio, sincronización RTC o descarga XTRA automáticos
  solo porque exista un parámetro o una función.
- Protección celular frente a errores HTTP: las variables HTTP de backoff
  declaradas pero no utilizadas no constituyen una función operativa.
- URLs nuevas de inserción: probar su construcción sin enviar datos ficticios
  al backend de producción.

Pruebas disponibles (Python y `g++`):

```powershell
python tests/test_offline_logging.py
python tests/test_measurement_payload.py
```

Registrar por separado compilación, simulación y prueba física. No convertir
una sospecha de energía en un diagnóstico confirmado sin logs/mediciones.
En una revisión solo documental basta contrastar fuentes, enlaces y coherencia;
no hace falta cargar firmware ni repetir compilaciones sin cambios de código.

## 9. Política de rollback

Si un cambio rompe estabilidad:

1. Revertir el commit problemático o preparar un fix nuevo con trazabilidad.
2. Confirmar boot y operación mínima.
3. Documentar incidente en `documentacion/CAMBIOS.md`.
4. Reaplicar la corrección en un commit nuevo.

No sobrescribir historia si ya fue compartida.

---

## 10. Convención recomendada de commits

- `feat:` nueva funcionalidad.
- `fix:` corrección.
- `docs:` documentación.
- `refactor:` refactor sin cambio funcional.
- `revert:` reversión controlada.

Ejemplos:

- `feat: agrega reconexion celular con pausa de una hora`
- `fix: corrige pin de boton para variante valpo`
- `docs: sincroniza manuales con version Pro V0.1.29R`

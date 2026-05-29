#line 1 "C:\\Users\\Ale\\Dropbox\\clases_universidad\\HIRI_PR0_FINAL_V01_ESTATIC\\documentacion\\DEVELOPER_GUIDELINES.md"
# Developer Guidelines - FirmwarePro

Este archivo define el flujo mínimo para mantener trazabilidad y control de versiones en este proyecto.

---

## 1. Regla de versión

Cada cambio funcional del firmware debe ir acompañado de incremento de versión.

- Ubicación actual: variable `VERSION` en `HIRI_PR0_FINAL_V01_ESTATIC.ino`.
- Versión actual documentada: **Pro V0.1.15V**.
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
8. Commit y push.
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
- HTTP transmite o deja trazabilidad en `failed_h<ID>.csv`.
- WiFi SD operativo si aplica.
- GNSS validado si está habilitado para el despliegue.
- Documentación y changelog actualizados.

---

## 8. Política de rollback

Si un cambio rompe estabilidad:

1. Revertir el commit problemático o preparar un fix nuevo con trazabilidad.
2. Confirmar boot y operación mínima.
3. Documentar incidente en `documentacion/CAMBIOS.md`.
4. Reaplicar la corrección en un commit nuevo.

No sobrescribir historia si ya fue compartida.

---

## 9. Convención recomendada de commits

- `feat:` nueva funcionalidad.
- `fix:` corrección.
- `docs:` documentación.
- `refactor:` refactor sin cambio funcional.
- `revert:` reversión controlada.

Ejemplos:

- `feat: agrega backoff HTTP por fallas consecutivas`
- `fix: corrige pin de boton para variante valpo`
- `docs: sincroniza manuales con version Pro V0.1.15V`

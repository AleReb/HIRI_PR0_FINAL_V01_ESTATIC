# Developer Guidelines - FirmwarePro

Este archivo define el flujo mínimo obligatorio para mantener trazabilidad y control de versiones en este proyecto.

---

## 1) Regla obligatoria de versión

**Cada cambio funcional del firmware debe ir acompañado de incremento de versión.**

- Ubicación actual: variable `VERSION` en `FirmwarePro.ino`
- Formato recomendado: `Pro V0.0.xx`
- No mezclar múltiples features grandes sin subir versión.

---

## 2) Regla obligatoria de changelog

**Cada cambio funcional debe anotarse en `CAMBIOS.md`** en la misma sesión de trabajo.

Debe incluir:
- Número de versión
- Fecha
- Qué cambió
- Riesgo/impacto (si aplica)
- Commit relacionado (ideal)

Si no se actualiza `CAMBIOS.md`, el cambio se considera incompleto.

---

## 3) Commit de funcionamiento confirmado

Cuando una versión sea validada en hardware real, registrar explícitamente:
- Commit SHA
- Resultado de prueba (ej: bootea OK, flashea OK, WiFi OK)
- Puerto/dispositivo de prueba (si aplica)

Registrar en:
- `CAMBIOS.md` (recomendado)
- y/o release notes

---

## 4) Flujo mínimo recomendado por cambio

1. Editar código.
2. Subir `VERSION` en `FirmwarePro.ino`.
3. Actualizar `CAMBIOS.md`.
4. Compilar con `arduino-cli`.
5. Flashear en hardware de prueba.
6. Validar comportamiento crítico.
7. Commit + push.
8. Registrar commit funcional confirmado.

---

## 5) Comandos estándar

## Compilar
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 FirmwarePro.ino
```

## Flashear (ejemplo COM5)
```bash
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32 FirmwarePro.ino
```

---

## 6) Criterios de "listo para producción"

Antes de marcar una versión como estable:
- Boot estable (sin loop reset)
- SD operativa (crea y escribe CSV)
- GNSS operativo
- HTTP operativo (si aplica)
- UI navegable
- WiFi SD operativo (si aplica)
- Documentación y changelog actualizados

---

## 7) Política de rollback

Si un cambio rompe estabilidad:
1. Revertir commit problemático.
2. Confirmar boot + operación mínima.
3. Documentar incidente en `CAMBIOS.md`.
4. Reaplicar fix en commit nuevo, nunca sobreescribir historia sin trazabilidad.

---

## 8) Convención recomendada de mensajes de commit

- `feat:` nueva funcionalidad
- `fix:` corrección
- `docs:` documentación
- `refactor:` refactor sin cambio funcional
- `revert:` reversión controlada

Ejemplo:
- `feat: WiFi SD exclusivo con captive portal`
- `fix: evita reset en loop de modo WiFi`
- `docs: actualiza CAMBIOS y manual técnico`

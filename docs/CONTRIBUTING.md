# Guía de Contribución — Robot Aspiradora Autónomo

Este documento define el flujo de trabajo con Git que debe seguir todo el equipo durante el proyecto. Cúmplelo desde el primer commit: el 10% de la nota de "Flujo de trabajo Git" se evalúa sobre el historial completo, no solo sobre el resultado final.

## 1. Estructura de branches

El proyecto usa tres niveles de ramas:

- **`main`**: rama principal. Solo recibe *merges* desde `develop`, y únicamente cuando una funcionalidad está completa y verificada. No se hace commit directo aquí bajo ninguna circunstancia.
- **`develop`**: rama de integración. Recibe *merges* de las ramas de feature una vez que cada función pasa sus pruebas.
- **Ramas de feature**: una rama por cada función o componente principal del proyecto (una por cada historia de usuario del backlog, en general), creadas desde `develop` y mergeadas de vuelta a `develop` al completarse.

Flujo: `feature/<algo>` → PR a `develop` → (cuando el sprint/fase cierra y todo funciona) → PR de `develop` a `main`.

## 2. Convención de nombres de branches

| Tipo | Formato |
|---|---|
| Feature nueva | `feature/<descripcion-corta>` |
| Corrección | `fix/<descripcion-corta>` |
| Documentación | `docs/<descripcion-corta>` |
| Tarea de mantenimiento | `chore/<descripcion-corta>` |

La descripción corta va en minúsculas, en inglés o español consistente (elijan uno como equipo), separada por guiones, sin referirse al número de issue (eso va en el PR, no en el nombre de la rama).

**Ejemplos usados en este proyecto** (tomados del backlog de historias de usuario):

```
feature/hw-motor-isolation
feature/hw-power-supply
feature/librobot-api
feature/librobot-motor-control
feature/librobot-sensors-leds
feature/librobot-audio
feature/webserver-stack-decision
feature/web-server-core
feature/autonomous-navigation
feature/incremental-map
feature/meta-robot-layer
feature/yocto-recipes
feature/systemd-service
fix/pwm-duty-cycle-clamp
fix/sensor-false-positive
docs/readme-final
docs/design-document
chore/repo-setup
```

## 3. Convención de mensajes de commit

Los mensajes de commit deben seguir el formato de **Conventional Commits**:

```
<tipo>(<ámbito>): <descripción breve en imperativo>
```

| Tipo | Cuándo usarlo |
|---|---|
| `feat` | Se agrega una nueva función o componente |
| `fix` | Se corrige un bug |
| `test` | Se agrega o modifica un caso de prueba |
| `docs` | Se agrega o actualiza documentación |
| `refactor` | Se reestructura código sin cambiar comportamiento |
| `chore` | Tareas de mantenimiento que no afectan el código de producto (configuración de repo, CI, dependencias) |

El `<ámbito>` es opcional pero recomendado — identifica qué parte del sistema toca el commit: `motor`, `sensors`, `audio`, `web`, `yocto`, `systemd`, `docs`, etc.

**Ejemplos aplicados al proyecto:**

```
feat(motor): implementar control diferencial por PWM
feat(sensors): agregar lectura de HC-SR04 frontal y lateral
feat(audio): integrar reproduccion concurrente con mpg123
feat(web): agregar panel de control de audio con seleccion de playlist
fix(motor): corregir clamp de duty cycle fuera de rango
fix(sensors): eliminar falso positivo en deteccion de obstaculo
test(motor): agregar pruebas de rango de PWM
docs(readme): documentar API de librobot.so
docs(yocto): agregar instrucciones para agregar meta-robot
refactor(web): extraer logica de autenticacion a modulo separado
chore(repo): configurar plantillas de issues y Git Flow
```

## 4. Reglas del ciclo de vida de un cambio

1. Toda historia de usuario del backlog se trabaja en su propia rama `feature/<...>`, creada desde `develop` actualizado.
2. Los commits dentro de esa rama siguen la convención de la sección 3, uno por cambio lógico (evitar commits gigantes tipo "avance del dia").
3. Al terminar, se abre un Pull Request hacia `develop`, referenciando el issue/historia correspondiente (ej. `Closes #12`).
4. El PR debe ser revisado por al menos un integrante distinto al autor antes de mergear.
5. Solo se mergea a `main` cuando `develop` está en un estado estable y demostrable (idealmente al cierre de cada semana del cronograma).
6. Nunca se hace `git push --force` sobre `develop` o `main`.

## 5. Gestión de issues

- Cada historia de usuario del backlog se convierte en un issue de GitHub con su checklist de criterios de aceptación tal cual.
- Los bugs encontrados durante el desarrollo se documentan como issues nuevos con la plantilla de "bug", no se resuelven silenciosamente dentro de otra rama.
- El tablero del proyecto (GitHub Projects) debe reflejar en todo momento qué historias están en `To Do`, `In Progress` y `Done` — usar el cronograma semanal como referencia para decidir qué mover a `In Progress` cada semana.

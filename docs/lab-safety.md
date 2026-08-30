# Límites del laboratorio

- VM Hyper-V desechable y sin adaptadores de red.
- Certificado local y `testsigning`; no existe firma de producción.
- Directorio fijo `C:\RootkitLabSandbox` y marcador obligatorio.
- Solo se aceptan nombres exactos de archivos que ya existen en el sandbox.
- La interfaz limita la selección a 16 nombres y no envía rutas al kernel.
- El efecto comienza desactivado y las reglas no persisten tras reiniciar.
- `SYSTEM_START` se usa en el ensayo de persistencia y se revierte antes de retirar el servicio.
- Un archivo homónimo fuera del sandbox actúa como control negativo.
- Los runners terminan con unload, retirada del servicio y limpieza del fixture.
- No se admite la ejecución fuera de una VM de laboratorio.
- Sin red, C2, inyección, credenciales, keylogging, memoria arbitraria, DKOM, hooks ni bypass de seguridad.

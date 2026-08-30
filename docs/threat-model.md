# Modelo de amenazas de RootkitLab 2.0

## Activos

- estabilidad e integridad de la VM
- contenido de los archivos del laboratorio
- límite entre modo usuario y kernel
- capacidad de desactivar y retirar el minifilter
- integridad y trazabilidad de las evidencias

## Fronteras de confianza

1. selección realizada en `RootkitLab.exe`
2. ABI del Filter Manager Communication Port
3. buffers de consulta de directorio entregados al minifilter
4. volumen NTFS consultado por la vista MFT
5. scripts y resultados de la VM
6. transferencia de resultados al host

## Riesgos y mitigaciones

| Riesgo | Mitigación | Evidencia |
|---|---|---|
| uso sobre una ruta arbitraria | sandbox compilado; el mensaje no contiene rutas | `filter_protocol.h` |
| activación accidental | estado inicial desactivado, marcador y reglas obligatorios | controles 20 y 22 |
| selección de un objeto ajeno | nombre exacto y comprobación de existencia dentro del sandbox | control 18 |
| cambio de reglas durante una ocultación | `replace rules` devuelve `STATUS_DEVICE_BUSY` | control 15 |
| buffer malformado | validación completa antes de compactar | `RootkitLabFilter.c` |
| acceso inseguro a memoria de usuario | MDL, bloqueo, mapeo safe y SEH | `RootkitLabFilter.c` |
| carrera entre control y filtrado | `EX_PUSH_LOCK` compartido/exclusivo | `RootkitLabFilter.c` |
| cliente sin privilegios | descriptor del communication port y manifiesto UAC | código y proyecto |
| ABI incompatible | magic, tamaño y versión en petición y respuesta | `filter_protocol.h` |
| efecto fuera del escenario | ruta exacta, marcador y control externo | evidencia 13 |
| falso positivo de detección | dos vistas independientes y apertura directa | evidencia 12 |
| persistencia encubierta | servicio visible y script reversible | carpeta `persistence` |
| estado activo tras reiniciar | reglas y activación solo en memoria | evidencia 12 de persistencia |
| ejecución parcial interpretada como éxito | `FAILED.txt`, `COMPLETE.txt` y validador | runners y summary |

## Invariantes

- el filtro comienza desactivado y sin reglas
- las reglas solo contienen nombres existentes dentro del sandbox
- el control externo no se altera
- la apertura directa de un nombre omitido continúa funcionando
- la vista MFT no consulta estado ni reglas del driver
- `disable` devuelve las vistas a `consistent`
- el reinicio conserva el componente, no la selección ni el efecto
- la retirada elimina servicio, binario desplegado y fixture
- ninguna prueba requiere conectividad de red

## Riesgos residuales

- validación limitada a Windows Server 2022 y NTFS
- altitud académica no asignada para distribución
- firma de pruebas y ausencia de validación HLK
- detector limitado a `USN_RECORD_V2`
- Driver Verifier limitado a una campaña dirigida; no se ha realizado una ejecución prolongada ni HLK

## Exclusiones

No se implementan C2, sockets, instalación remota, keylogging, credenciales, inyección, DKOM, manipulación de procesos, hooks de kernel, bypass de DSE ni desactivación de controles de seguridad.

# Diseño del minifilter

## Registro y ciclo de vida

`DriverEntry` registra el minifilter con `FltRegisterFilter`, crea el communication port y llama a `FltStartFiltering`. `RklInstanceSetup` rechaza sistemas de archivos distintos de NTFS. La descarga desactiva el efecto, borra las reglas, cierra los puertos y libera el registro del filtro.

La instalación registra `RootkitLabFilter` como `SERVICE_FILE_SYSTEM_DRIVER`, dependiente de `FltMgr`, en el grupo `FSFilter Activity Monitor`. La altitud 370030 se utiliza únicamente en el laboratorio y no se presenta como una asignación de producción.

## Reglas selectivas

El protocolo admite un máximo de 16 nombres UTF-16. `RklReplaceRules` aplica las siguientes condiciones:

- el efecto debe estar desactivado
- el marcador debe existir
- cada nombre debe tener longitud válida
- no se aceptan `\`, `/`, `*`, `?`, caracteres de control ni terminaciones con punto o espacio
- no se puede seleccionar `.rootkitlab-lab`
- el archivo debe existir dentro de `C:\RootkitLabSandbox`

La tabla se sustituye bajo un `EX_PUSH_LOCK` exclusivo. Las consultas usan el mismo bloqueo en modo compartido. La revisión aumenta después de cada sustitución correcta.

## Tratamiento de buffers

Las respuestas de directorio son secuencias enlazadas por `NextEntryOffset`. El driver soporta:

- `FileDirectoryInformation`
- `FileFullDirectoryInformation`
- `FileBothDirectoryInformation`
- `FileNamesInformation`
- `FileIdBothDirectoryInformation`
- `FileIdFullDirectoryInformation`

La primera pasada comprueba límites, longitud de nombre, alineación UTF-16 y encadenamiento. Solo si el buffer completo es válido se ejecuta una segunda pasada que compacta las entradas visibles mediante `RtlMoveMemory`. Así se evita dejar una respuesta parcialmente modificada.

Para buffers de usuario se usa `FltDoCompletionProcessingWhenSafe`, `FltLockUserBuffer` y `MmGetSystemAddressForMdlSafe`. Las zonas sujetas a acceso indirecto están protegidas con SEH.

## Contadores

Los contadores de 64 bits se actualizan con operaciones interlocked:

- consultas de directorio
- consultas sobre el sandbox
- entradas omitidas
- transiciones de activación y desactivación
- sustituciones de reglas
- órdenes rechazadas

No se almacena contenido de archivos en kernel.

## Decisiones de diseño

Se eligió un minifilter en lugar de hooks de SSDT/IDT o DKOM porque Filter Manager ofrece un contrato documentado y un ciclo de vida reversible. La selección por nombre exacto permite demostrar configuración dinámica sin aceptar rutas arbitrarias. El marcador y la comprobación de existencia reducen activaciones accidentales y mantienen el experimento dentro del fixture preparado.

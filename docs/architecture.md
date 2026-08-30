# Arquitectura de RootkitLab 2.0

## Visión general

La arquitectura final concentra las funciones de usuario en una aplicación y deja la modificación de la enumeración en un único minifilter:

```text
RootkitLab.exe
  |-- configuración y estado ---- FilterSendMessage ----> RootkitLabFilter.sys
  |-- vista A: FindFirstFileW                                  |
  |-- vista B: FSCTL_ENUM_USN_DATA                             | IRP_MJ_DIRECTORY_CONTROL
  `-- apertura directa                                        v
                                                        NTFS / C:\RootkitLabSandbox
```

Esta distribución produce exactamente un `.exe` y un `.sys`. La interfaz, el cliente del driver y la detección cross-view están compilados dentro de `RootkitLab.exe`. Dear ImGui y sus backends Win32/DX11 también se enlazan de forma estática.

## Límite entre modo usuario y kernel

El minifilter crea `\RootkitLabFilterPort` y acepta una única sesión administrativa. La petición contiene magic, tamaño, versión ABI, comando y un conjunto acotado de nombres. La respuesta devuelve estado, reglas efectivas, revisión y contadores.

Los comandos son `status`, `enable`, `disable`, `clear counters` y `replace rules`. El mensaje no transporta rutas: el directorio se encuentra compilado en ambos componentes. Antes de sustituir las reglas, el driver verifica cada nombre y confirma que el objeto existe como archivo dentro del sandbox.

## Camino de la ocultación

1. Una aplicación solicita una enumeración de directorio.
2. Filter Manager entrega `IRP_MJ_DIRECTORY_CONTROL` al minifilter.
3. La rutina pre-operation registra la consulta y solicita post-operation solo para `IRP_MN_QUERY_DIRECTORY` cuando el efecto está activo.
4. La rutina post-operation comprueba el directorio normalizado y el marcador.
5. El buffer completo se valida antes de modificarlo.
6. Las entradas cuyo nombre coincide con una regla se eliminan mediante compactación in-place.
7. Se actualizan `NextEntryOffset`, `IoStatus.Information`, los contadores y el estado dirty del callback data.

La apertura directa no se intercepta. El archivo continúa existiendo y su contenido permanece intacto.

## Estados

```text
no instalado
  -> instalado / desactivado / sin reglas
  -> reglas aplicadas
  -> activado
  -> desactivado
  -> descargado y retirado

SYSTEM_START
  -> reinicio
  -> cargado / desactivado / sin reglas
```

Las reglas solo pueden cambiar con el efecto desactivado. El arranque no restaura selección ni activación, de modo que persiste el componente, no el efecto.

## Detección integrada

La interfaz obtiene una vista mediante `FindFirstFileW` y otra recorriendo la MFT con `FSCTL_ENUM_USN_DATA`. La segunda no solicita una enumeración del directorio ni consulta el puerto del driver. Una diferencia MFT → Win32, acompañada de una apertura directa correcta, se clasifica como `cross_view_inconsistency`.

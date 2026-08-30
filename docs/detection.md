# Detección cross-view

## Hipótesis

Si una capa situada por encima de NTFS modifica la enumeración de un directorio, la vista habitual del namespace puede discrepar de una fuente que recorra los registros de la MFT. La discrepancia debe limitarse a las reglas activas y desaparecer al desactivar el filtro.

## Vista Win32

`RootkitLab.exe` ejecuta `FindFirstFileW` y `FindNextFileW` sobre `C:\RootkitLabSandbox\*`. Esta ruta reproduce lo que ven Explorer y muchas aplicaciones de usuario, por lo que atraviesa el minifilter.

## Vista MFT

La aplicación abre el directorio con `FILE_FLAG_BACKUP_SEMANTICS`, obtiene su file ID y abre `\\.\C:`. A continuación procesa los registros de `FSCTL_ENUM_USN_DATA` cuyo `ParentFileReferenceNumber` coincide con el sandbox.

Esta vista no enumera el directorio objetivo y no recibe las reglas del driver. Es independiente para la hipótesis probada.

## Control de existencia

Se intenta abrir directamente una entrada que falta en Win32. Si la apertura funciona, el objeto no ha sido eliminado: únicamente ha desaparecido de la enumeración.

## Clasificaciones

| Clasificación | Condición |
|---|---|
| `consistent` | Win32 y MFT contienen el mismo conjunto |
| `cross_view_inconsistency` | la MFT contiene nombres ausentes en Win32 y la apertura directa funciona |
| `missing_object` | hay diferencia, pero el objeto no se puede abrir |
| `inconclusive` | falla alguna de las fuentes |

## Controles del experimento

En la prueba principal se seleccionan `proyecto_confidencial.txt` y `presupuesto_2026.xlsx`. El archivo `C:\RootkitLabOutside\proyecto_confidencial.txt` conserva el mismo nombre fuera del alcance y debe seguir visible. Después de `disable` y después del unload ambas vistas deben volver a ser coherentes.

## Límites

El detector está construido para este escenario. Un producto general tendría que resolver árboles de file IDs, cambios concurrentes, hard links, varias versiones de `USN_RECORD`, volúmenes sin journal y otros sistemas de archivos.

# Protocolo experimental 2.0

## Entorno

Las pruebas se ejecutan en una VM Hyper-V con Windows Server 2022 x64 build 20348, EWDK montado, firma de pruebas y cero adaptadores de red. El host solo copia fuentes y recupera resultados mediante PowerShell Direct.

## Variables

Variable independiente: estado de `RootkitLabFilter` (`disabled` o `enabled`) y conjunto de reglas exactas.

Variables observadas:

- conjuntos devueltos por Win32 y MFT
- apertura directa del objeto omitido
- visibilidad del control fuera del sandbox
- reglas y revisión de configuración
- estado del servicio y Filter Manager
- contadores del minifilter
- residuos después de la retirada

## Fixture

```text
C:\RootkitLabSandbox\.rootkitlab-lab
C:\RootkitLabSandbox\resumen_publico.txt
C:\RootkitLabSandbox\proyecto_confidencial.txt
C:\RootkitLabSandbox\presupuesto_2026.xlsx
C:\RootkitLabSandbox\contrato_cliente.pdf
C:\RootkitLabSandbox\notas_reunion.txt
C:\RootkitLabOutside\proyecto_confidencial.txt
```

## Secuencia de cada ciclo

1. retirar una instalación o fixture anterior
2. calcular el manifiesto SHA-256 de las fuentes
3. recompilar `Debug|x64`
4. firmar el minifilter con el certificado local de pruebas
5. crear el fixture y capturar la baseline
6. instalar el minifilter en modo demand
7. aplicar dos reglas exactas y activar
8. capturar la vista Win32, la comparación MFT y la apertura directa
9. comprobar el archivo homónimo fuera de alcance
10. verificar que las reglas no cambian mientras el efecto está activo
11. desactivar y comprobar la restauración
12. probar el rechazo de objeto inexistente, marcador ausente y conjunto vacío
13. calcular hashes e inspeccionar imports de los dos artefactos
14. descargar, retirar y volver a medir
15. limpiar el fixture y escribir `COMPLETE.txt`

## Ensayo de persistencia

La fase previa instala el servicio con `Start=1`, selecciona dos archivos, activa el efecto y registra la hora de arranque. Después de un reinicio real se exige:

- servicio `RUNNING`
- instancia visible en `fltmc`
- misma configuración `SYSTEM_START`
- efecto desactivado y cero reglas
- vistas coherentes antes de una nueva orden
- posibilidad de volver a configurar y activar
- retirada y limpieza completas

## Cobertura de clases de directorio

La sonda nativa consulta el mismo fixture mediante `NtQueryDirectoryFile` y las seis clases que procesa el minifilter:

```powershell
.\tools\query-directory-classes.ps1 `
  -RootkitLabExe .\out\Debug\x64\RootkitLab.exe `
  -Sandbox C:\RootkitLabSandbox `
  -OutputPath .\class-coverage.json
```

Para cada clase se exigen seis nombres exactos en baseline, cuatro con el efecto activo y ausencia de las dos reglas seleccionadas. El resultado global solo es válido con `Passed=true` y seis checks aceptados.

## Driver Verifier

La campaña dirigida se ejecuta después de crear un checkpoint de la VM:

```powershell
verifier.exe /standard /driver RootkitLabFilter.sys
Restart-Computer -Force
verifier.exe /querysettings
```

Con Verifier activo se repiten `status`, sustitución de reglas, `enable`, `snapshot`, cobertura de las seis clases, `disable` y limpieza. Se revisa el log `System` desde el arranque y se exige que no existan bugchecks ni eventos críticos. La retirada requiere dos pasos y una comprobación funcional final:

```powershell
verifier.exe /reset
Restart-Computer -Force
verifier.exe /querysettings
RootkitLab.exe --status
RootkitLab.exe --snapshot
```

## Criterios de aceptación

- tres ciclos completos, sin marcador `FAILED.txt`
- mismo digest de fuentes y un SHA-256 válido para cada artefacto producido
- exactamente dos artefactos ejecutables
- baseline y restauración `consistent`
- exactamente dos nombres ausentes durante la activación
- apertura directa y control externo positivos
- tres controles negativos rechazados
- persistencia confirmada entre horas de arranque distintas
- build Release con análisis estático completado
- seis clases de directorio con baseline y estado activo exactos
- campaña dirigida de Driver Verifier sin fallos de operación ni bugcheck
- `VALIDATION-SUMMARY.json` con `all_passed=true`

# RootkitLab 2.0

RootkitLab es un prototipo académico para Windows formado por un minifilter de kernel y una aplicación de escritorio. Su función principal es ocultar de forma selectiva nombres de archivo en las enumeraciones del directorio de laboratorio, sin borrar, renombrar ni alterar el contenido de los objetos. La misma aplicación permite elegir las reglas, activar o desactivar el efecto, consultar contadores y detectar la ocultación mediante una comparación cross-view.

La versión final tiene dos únicos artefactos ejecutables:

| Artefacto | Función |
|---|---|
| `RootkitLabFilter.sys` | Minifilter que intercepta consultas de directorio y omite los nombres seleccionados |
| `RootkitLab.exe` | Interfaz Dear ImGui, cliente del driver y detector cross-view |

La ocultación es real: `RootkitLabFilter.sys` modifica el buffer que vuelve de `IRP_MN_QUERY_DIRECTORY`. El alcance, en cambio, está limitado por diseño a `C:\RootkitLabSandbox`, requiere un marcador local y solo admite nombres exactos que ya existan dentro de ese directorio.

## Qué demuestra

- interacción documentada entre modo usuario y kernel mediante un Filter Manager Communication Port
- tratamiento seguro de seis clases de información de directorio con registros de longitud variable
- selección dinámica de hasta 16 nombres desde una interfaz gráfica
- desaparición de los nombres elegidos en Explorer, PowerShell y la enumeración Win32
- conservación de la apertura directa y del contenido de los archivos omitidos
- detección independiente al comparar `FindFirstFileW` con registros de la MFT obtenidos mediante `FSCTL_ENUM_USN_DATA`
- persistencia visible del componente como servicio `SYSTEM_START`
- estado seguro después de reiniciar: el driver se carga, pero no conserva reglas ni activa el efecto
- reversión completa mediante descarga del minifilter, retirada del servicio y limpieza del fixture

## Interfaz

`RootkitLab.exe` integra en una sola ventana:

- estado del servicio, tipo de inicio, conexión y marcador
- lista de archivos del sandbox con selección por checkbox
- acciones `Aplicar selección`, `Activar`, `Desactivar` y `Limpiar contadores`
- contadores de consultas, entradas omitidas, cambios de reglas y órdenes rechazadas
- vista Win32, vista MFT, diferencias y prueba de apertura directa
- registro breve de la sesión

La aplicación también expone una CLI para automatizar las pruebas sin añadir otro binario:

```text
RootkitLab.exe --status
RootkitLab.exe --set-rules [nombre1 nombre2 ...]
RootkitLab.exe --enable
RootkitLab.exe --disable
RootkitLab.exe --clear
RootkitLab.exe --snapshot
```

## Alcance fijo y controles

Los límites principales están definidos en `shared/filter_protocol.h`:

```text
directorio: C:\RootkitLabSandbox
marcador: C:\RootkitLabSandbox\.rootkitlab-lab
máximo: 16 nombres exactos
```

El driver rechaza reglas mientras el efecto está activo, nombres con separadores o comodines, objetos inexistentes, el propio marcador y activaciones sin marcador o sin reglas. Un archivo con el mismo nombre en `C:\RootkitLabOutside` permanece visible y actúa como control negativo.

No se implementan red, C2, instalación remota, inyección, captura de teclado o credenciales, acceso arbitrario a memoria, DKOM, hooks de SSDT/IDT, bypass de firma ni desactivación de productos de seguridad.

## Compilación

Entorno utilizado:

- Windows Server 2022 x64, build 20348
- EWDK/WDK con Visual Studio Build Tools
- VM desechable con firma de pruebas
- PowerShell 5.1 para los scripts

Desde el entorno de compilación del EWDK:

```cmd
cd /d C:\TFM\RootkitLab-v2-final
scripts\build.cmd Debug x64
```

La solución genera exclusivamente `RootkitLab.exe` y `RootkitLabFilter.sys` en `out\Debug\x64`. La aplicación enlaza Dear ImGui de forma estática; no necesita DLL auxiliares.

El análisis de la configuración Release se ejecuta con:

```cmd
msbuild.exe RootkitLab.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:RunCodeAnalysis=true /v:minimal
```

## Preparación del laboratorio

Los comandos siguientes se ejecutan como administrador dentro de la VM:

```powershell
.\scripts\setup-filter-lab.ps1
.\scripts\create-test-certificate.ps1 -OutputDirectory .\out\cert
.\scripts\sign-driver.ps1 -DriverPath .\out\Debug\x64\RootkitLabFilter.sys -SignToolPath <ruta-signtool>
.\scripts\install-filter.ps1 -DriverPath .\out\Debug\x64\RootkitLabFilter.sys -StartType demand
.\out\Debug\x64\RootkitLab.exe
```

La retirada se realiza con:

```powershell
.\scripts\remove-filter.ps1
.\scripts\cleanup-filter-lab.ps1
```

## Validación reproducible

Cada ciclo recompila, firma, instala, prueba y retira los dos artefactos:

```powershell
.\vm\run-v2-cycle.ps1 -RunId run-01
.\vm\run-v2-cycle.ps1 -RunId run-02
.\vm\run-v2-cycle.ps1 -RunId run-03
```

El ensayo de persistencia se divide alrededor de un reinicio real:

```powershell
.\vm\run-v2-persistence-pre.ps1
Restart-Computer -Force
.\vm\run-v2-persistence-post.ps1
```

Las evidencias se verifican fuera de la VM con:

```powershell
.\tools\validate-v2-results.ps1 -EvidenceRoot <ruta-evidencias-v2.0>
```

La cobertura directa de las seis clases de directorio se ejecuta en la VM con:

```powershell
.\tools\query-directory-classes.ps1 `
  -RootkitLabExe .\out\Debug\x64\RootkitLab.exe `
  -OutputPath .\class-coverage.json
```

El protocolo completo incluye además una campaña dirigida de Driver Verifier sobre `RootkitLabFilter.sys`, documentada en [Protocolo experimental](docs/experiment.md).

El resultado esperado es `consistent` antes de activar, `cross_view_inconsistency` con dos nombres durante la activación y `consistent` después de desactivar o descargar el minifilter.

## Documentación

- [Arquitectura](docs/architecture.md)
- [Diseño del minifilter](docs/minifilter.md)
- [Detección cross-view](docs/detection.md)
- [Protocolo experimental](docs/experiment.md)
- [Modelo de amenazas](docs/threat-model.md)
- [Límites del laboratorio](docs/lab-safety.md)
- [Referencias](docs/references.md)

## Procedencia

El desarrollo sigue la progresión docente de `Rootkits-Development-Starter-Pack` y las interfaces públicas documentadas por Microsoft. El código de RootkitLab es propio. Dear ImGui se distribuye bajo licencia MIT y conserva su licencia en `third_party/imgui/LICENSE.txt`.

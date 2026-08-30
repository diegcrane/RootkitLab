[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$sandbox = 'C:\RootkitLabSandbox'
$outside = 'C:\RootkitLabOutside'

New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
New-Item -ItemType Directory -Path $outside -Force | Out-Null

'RootkitLab academic lab marker v2.0' |
    Set-Content -LiteralPath (Join-Path $sandbox '.rootkitlab-lab') -Encoding ascii
'Public project summary' |
    Set-Content -LiteralPath (Join-Path $sandbox 'resumen_publico.txt') -Encoding ascii
'Internal project notes used for the selective-hiding demonstration' |
    Set-Content -LiteralPath (Join-Path $sandbox 'proyecto_confidencial.txt') -Encoding ascii
'Synthetic budget used for the selective-hiding demonstration' |
    Set-Content -LiteralPath (Join-Path $sandbox 'presupuesto_2026.xlsx') -Encoding ascii
'Synthetic customer contract used as a visible control' |
    Set-Content -LiteralPath (Join-Path $sandbox 'contrato_cliente.pdf') -Encoding ascii
'Meeting notes used as a visible control' |
    Set-Content -LiteralPath (Join-Path $sandbox 'notas_reunion.txt') -Encoding ascii
'Same selected name outside the fixed scope' |
    Set-Content -LiteralPath (Join-Path $outside 'proyecto_confidencial.txt') -Encoding ascii

[pscustomobject]@{
    Sandbox = $sandbox
    MarkerPresent = Test-Path -LiteralPath (Join-Path $sandbox '.rootkitlab-lab')
    SandboxEntries = @(Get-ChildItem -LiteralPath $sandbox -Force | Select-Object -ExpandProperty Name)
    OutsideControlPresent = Test-Path -LiteralPath (Join-Path $outside 'proyecto_confidencial.txt')
}

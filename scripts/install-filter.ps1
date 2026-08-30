[CmdletBinding()]
param(
    [string]$DriverPath = (Join-Path $PSScriptRoot '..\out\Debug\x64\RootkitLabFilter.sys'),
    [ValidateSet('demand','system')][string]$StartType = 'demand'
)

$ErrorActionPreference = 'Stop'
$service = 'RootkitLabFilter'
$deployed = 'C:\Windows\System32\drivers\RootkitLabFilter.sys'
$serviceKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\RootkitLabFilter'
$instanceName = 'RootkitLabFilter Instance'
$altitude = '370030'

if (-not (Test-Path -LiteralPath $DriverPath -PathType Leaf)) {
    throw "Driver not found: $DriverPath"
}
& sc.exe query $service *> $null
if ($LASTEXITCODE -eq 0) {
    throw "Service $service already exists; remove it first."
}

Copy-Item -LiteralPath $DriverPath -Destination $deployed -Force
& sc.exe create $service type= filesys start= $StartType error= normal `
    binPath= $deployed group= 'FSFilter Activity Monitor' depend= FltMgr `
    DisplayName= 'RootkitLab selective file-hiding minifilter'
if ($LASTEXITCODE -ne 0) {
    throw "sc.exe create failed with exit code $LASTEXITCODE"
}

$instances = Join-Path $serviceKey 'Instances'
$instance = Join-Path $instances $instanceName
New-Item -Path $instance -Force | Out-Null
New-ItemProperty -Path $instances -Name 'DefaultInstance' -Value $instanceName `
    -PropertyType String -Force | Out-Null
New-ItemProperty -Path $instance -Name 'Altitude' -Value $altitude `
    -PropertyType String -Force | Out-Null
New-ItemProperty -Path $instance -Name 'Flags' -Value 0 `
    -PropertyType DWord -Force | Out-Null

& fltmc.exe load $service
if ($LASTEXITCODE -ne 0) {
    throw "fltmc load failed with exit code $LASTEXITCODE"
}

& sc.exe query $service
& fltmc.exe filters

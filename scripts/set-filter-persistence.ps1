[CmdletBinding()]
param(
    [ValidateSet('demand','system')][string]$StartType
)

$ErrorActionPreference = 'Stop'
$service = 'RootkitLabFilter'

& sc.exe query $service *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Service $service is not installed."
}
& sc.exe config $service start= $StartType
if ($LASTEXITCODE -ne 0) {
    throw "sc.exe config failed with exit code $LASTEXITCODE"
}

$configuration = Get-ItemProperty `
    -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\RootkitLabFilter'
[pscustomobject]@{
    Service = $service
    RequestedStartType = $StartType
    RegistryStart = $configuration.Start
    ExpectedRegistryStart = if ($StartType -eq 'system') { 1 } else { 3 }
    Matches = $configuration.Start -eq $(if ($StartType -eq 'system') { 1 } else { 3 })
}


[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$service = 'RootkitLabFilter'
$deployed = 'C:\Windows\System32\drivers\RootkitLabFilter.sys'

try {
    $controller = Join-Path $PSScriptRoot '..\out\Debug\x64\RootkitLab.exe'
    if (Test-Path -LiteralPath $controller -PathType Leaf) {
        $process = Start-Process -FilePath $controller -ArgumentList '--disable' `
            -Wait -PassThru -WindowStyle Hidden
    }
} catch {
}

& fltmc.exe unload $service *> $null
Start-Sleep -Milliseconds 500
& sc.exe delete $service *> $null
Start-Sleep -Milliseconds 500
if (Test-Path -LiteralPath $deployed -PathType Leaf) {
    Remove-Item -LiteralPath $deployed -Force
}

[pscustomobject]@{
    Service = $service
    DriverFilePresent = Test-Path -LiteralPath $deployed
    Removed = -not (Test-Path -LiteralPath $deployed)
    TimeUtc = (Get-Date).ToUniversalTime().ToString('o')
}

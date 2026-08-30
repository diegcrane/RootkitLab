[CmdletBinding()]
param(
    [string]$SourceDirectory = 'C:\TFM\RootkitLab-v2-final'
)

$ErrorActionPreference = 'Stop'
$result = 'C:\TFM\Results\v2.0\persistence'
$signTool = 'D:\Program Files\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'

if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
    throw "Source directory not found: $SourceDirectory"
}
if (Test-Path -LiteralPath $result) {
    $resolved = (Resolve-Path -LiteralPath $result).Path
    if ($resolved -ne $result) {
        throw "Unexpected persistence result path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $result -Force | Out-Null
Set-Location $SourceDirectory

function Invoke-AppToFile {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string[]]$Arguments
    )
    $outputPath = Join-Path $result $Name
    $errorPath = "$outputPath.stderr.tmp"
    $process = Start-Process -FilePath $script:appPath `
        -ArgumentList $Arguments -Wait -PassThru `
        -RedirectStandardOutput $outputPath -RedirectStandardError $errorPath
    if ($process.ExitCode -ne 0) {
        $detail = if (Test-Path -LiteralPath $errorPath) {
            Get-Content -LiteralPath $errorPath -Raw
        } else { '' }
        throw "$Name returned exit code $($process.ExitCode): $detail"
    }
    Remove-Item -LiteralPath $errorPath -Force -ErrorAction SilentlyContinue
}

try {
    & .\scripts\remove-filter.ps1 *> $null
    & .\scripts\cleanup-filter-lab.ps1 *> $null

    & cmd.exe /d /s /c `
        'call D:\BuildEnv\SetupBuildEnv.cmd amd64 >nul && scripts\build.cmd Debug x64' 2>&1 |
        Out-File -LiteralPath (Join-Path $result '01-build.log') -Encoding utf8 -Width 4096
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    $script:appPath = Join-Path $SourceDirectory 'out\Debug\x64\RootkitLab.exe'

    & .\scripts\create-test-certificate.ps1 -OutputDirectory '.\out\cert' *> $null
    & .\scripts\sign-driver.ps1 `
        -DriverPath '.\out\Debug\x64\RootkitLabFilter.sys' `
        -SignToolPath $signTool 2>&1 |
        Out-File -LiteralPath (Join-Path $result '02-sign.log') -Encoding utf8 -Width 4096
    if ($LASTEXITCODE -ne 0) { throw 'Signing failed.' }

    & .\scripts\setup-filter-lab.ps1 | ConvertTo-Json -Depth 4 |
        Out-File -LiteralPath (Join-Path $result '03-setup.json') -Encoding utf8
    & .\scripts\install-filter.ps1 `
        -DriverPath '.\out\Debug\x64\RootkitLabFilter.sys' `
        -StartType system 2>&1 |
        Out-File -LiteralPath (Join-Path $result '04-install-system.log') -Encoding utf8 -Width 4096

    Invoke-AppToFile '05-set-rules-before-reboot.json' @(
        '--set-rules', 'proyecto_confidencial.txt', 'presupuesto_2026.xlsx')
    Invoke-AppToFile '06-enable-before-reboot.json' @('--enable')
    Invoke-AppToFile '07-crossview-before-reboot.json' @('--snapshot')
    Invoke-AppToFile '08-status-before-reboot.json' @('--status')

    [ordered]@{
        prepared = (Get-Date).ToString('o')
        boot_time_before = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
        service_start = (Get-ItemProperty `
            'HKLM:\SYSTEM\CurrentControlSet\Services\RootkitLabFilter').Start
    } | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $result 'PENDING-REBOOT.json') -Encoding utf8
} catch {
    $_ | Format-List * -Force |
        Out-File -LiteralPath (Join-Path $result 'FAILED-PRE.txt') -Encoding utf8 -Width 4096
    try { & .\scripts\remove-filter.ps1 *> $null } catch {}
    exit 1
}

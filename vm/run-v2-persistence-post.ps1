[CmdletBinding()]
param(
    [string]$SourceDirectory = 'C:\TFM\RootkitLab-v2-final'
)

$ErrorActionPreference = 'Stop'
$result = 'C:\TFM\Results\v2.0\persistence'

if (-not (Test-Path -LiteralPath (Join-Path $result 'PENDING-REBOOT.json'))) {
    throw 'The pre-reboot evidence is missing.'
}
Set-Location $SourceDirectory

$script:appPath = Join-Path $SourceDirectory 'out\Debug\x64\RootkitLab.exe'
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
    [ordered]@{
        verified = (Get-Date).ToString('o')
        boot_time_after = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
        service_start = (Get-ItemProperty `
            'HKLM:\SYSTEM\CurrentControlSet\Services\RootkitLabFilter').Start
    } | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $result '09-context-after-reboot.json') -Encoding utf8

    & sc.exe query RootkitLabFilter 2>&1 |
        Out-File -LiteralPath (Join-Path $result '10-service-after-reboot.txt') -Encoding utf8 -Width 4096
    if ($LASTEXITCODE -ne 0) { throw 'The service is not running after reboot.' }
    & fltmc.exe filters 2>&1 |
        Out-File -LiteralPath (Join-Path $result '11-filters-after-reboot.txt') -Encoding utf8 -Width 4096
    if ($LASTEXITCODE -ne 0) { throw 'Could not query loaded minifilters.' }

    Invoke-AppToFile '12-status-after-reboot.json' @('--status')
    Invoke-AppToFile '13-crossview-disabled-after-reboot.json' @('--snapshot')
    Invoke-AppToFile '14-set-rules-after-reboot.json' @(
        '--set-rules', 'proyecto_confidencial.txt', 'presupuesto_2026.xlsx')
    Invoke-AppToFile '15-enable-after-reboot.json' @('--enable')
    Invoke-AppToFile '16-crossview-active-after-reboot.json' @('--snapshot')
    Invoke-AppToFile '17-disable-after-reboot.json' @('--disable')
    & .\scripts\set-filter-persistence.ps1 -StartType demand | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $result '18-restore-demand.json') -Encoding utf8
    & .\scripts\remove-filter.ps1 | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $result '19-remove.json') -Encoding utf8
    & .\scripts\cleanup-filter-lab.ps1 | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $result '20-cleanup.json') -Encoding utf8
    Get-Date -Format o |
        Out-File -LiteralPath (Join-Path $result 'COMPLETE.txt') -Encoding utf8
} catch {
    $_ | Format-List * -Force |
        Out-File -LiteralPath (Join-Path $result 'FAILED-POST.txt') -Encoding utf8 -Width 4096
    try { & .\scripts\remove-filter.ps1 *> $null } catch {}
    exit 1
}

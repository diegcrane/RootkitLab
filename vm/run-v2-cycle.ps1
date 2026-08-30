[CmdletBinding()]
param(
    [ValidatePattern('^run-0[1-3]$')][string]$RunId,
    [string]$SourceDirectory = 'C:\TFM\RootkitLab-v2-final'
)

$ErrorActionPreference = 'Stop'
$resultsRoot = 'C:\TFM\Results\v2.0'
$resultsDirectory = Join-Path $resultsRoot $RunId
$signTool = 'D:\Program Files\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe'

function Write-CommandResult {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][scriptblock]$Command
    )
    $output = & $Command 2>&1
    $code = $LASTEXITCODE
    $output | Out-File -LiteralPath (Join-Path $resultsDirectory $Name) `
        -Encoding utf8 -Width 4096
    if ($null -ne $code -and $code -ne 0) {
        throw "$Name returned exit code $code"
    }
}

function Write-AppResult {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string[]]$Arguments
    )
    $outputPath = Join-Path $resultsDirectory $Name
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

function Write-ExpectedAppFailure {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [int]$ExpectedExitCode = 4
    )
    $temporaryOutput = Join-Path $resultsDirectory "$Name.stdout.tmp"
    $temporaryError = Join-Path $resultsDirectory "$Name.stderr.tmp"
    $process = Start-Process -FilePath $script:appPath `
        -ArgumentList $Arguments -Wait -PassThru `
        -RedirectStandardOutput $temporaryOutput -RedirectStandardError $temporaryError
    [ordered]@{
        expected_exit_code = $ExpectedExitCode
        actual_exit_code = $process.ExitCode
        passed = $process.ExitCode -eq $ExpectedExitCode
    } | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $resultsDirectory $Name) -Encoding utf8
    Remove-Item -LiteralPath $temporaryOutput,$temporaryError `
        -Force -ErrorAction SilentlyContinue
    if ($process.ExitCode -ne $ExpectedExitCode) {
        throw "$Name returned $($process.ExitCode) instead of $ExpectedExitCode"
    }
}

if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
    throw "Source directory not found: $SourceDirectory"
}
if (Test-Path -LiteralPath $resultsDirectory) {
    $resolved = (Resolve-Path -LiteralPath $resultsDirectory).Path
    if ($resolved -notlike 'C:\TFM\Results\v2.0\run-0?') {
        throw "Unexpected results path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $resultsDirectory -Force | Out-Null

try {
    Set-Location $SourceDirectory
    Get-Date -Format o |
        Out-File -LiteralPath (Join-Path $resultsDirectory '00-started.txt') -Encoding utf8
    [ordered]@{
        computer = $env:COMPUTERNAME
        os = [Environment]::OSVersion.Version.ToString()
        network_adapters = @(Get-NetAdapter -ErrorAction SilentlyContinue).Count
        source = (Resolve-Path -LiteralPath $SourceDirectory).Path
        testsigning = (& bcdedit.exe /enum '{current}' | Select-String 'testsigning').ToString()
    } | ConvertTo-Json -Depth 4 |
        Out-File -LiteralPath (Join-Path $resultsDirectory '01-context.json') -Encoding utf8

    $sourceRoot = (Resolve-Path -LiteralPath $SourceDirectory).Path.TrimEnd('\')
    $sourcePrefix = $sourceRoot + '\'
    $sourceFiles = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
        Where-Object {
            $_.FullName -notlike "$sourcePrefix`out\*" -and
            $_.FullName -notlike "$sourcePrefix`.git\*"
        } | Sort-Object FullName
    $sourceHashes = @($sourceFiles | ForEach-Object {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName
        [ordered]@{
            path = $_.FullName.Substring($sourcePrefix.Length)
            sha256 = $hash.Hash
        }
    })
    $digestMaterial = ($sourceHashes | ForEach-Object {
        "$($_.path)|$($_.sha256)"
    }) -join "`n"
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $treeDigest = [BitConverter]::ToString(
            $algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($digestMaterial))) `
            -replace '-', ''
    } finally {
        $algorithm.Dispose()
    }
    [ordered]@{
        schema = 'rootkitlab-source-manifest-v2.0'
        file_count = $sourceHashes.Count
        tree_sha256 = $treeDigest
        files = $sourceHashes
    } | ConvertTo-Json -Depth 5 |
        Out-File -LiteralPath (Join-Path $resultsDirectory '02-source-manifest.json') -Encoding utf8

    & .\scripts\remove-filter.ps1 *> $null
    & .\scripts\cleanup-filter-lab.ps1 *> $null

    Write-CommandResult '03-build.log' {
        & cmd.exe /d /s /c 'call D:\BuildEnv\SetupBuildEnv.cmd amd64 >nul && scripts\build.cmd Debug x64'
    }
    $script:appPath = Join-Path $SourceDirectory 'out\Debug\x64\RootkitLab.exe'
    & .\scripts\create-test-certificate.ps1 -OutputDirectory '.\out\cert' *> $null
    Write-CommandResult '04-sign-filter.log' {
        & .\scripts\sign-driver.ps1 `
            -DriverPath '.\out\Debug\x64\RootkitLabFilter.sys' `
            -SignToolPath $signTool
    }

    & .\scripts\setup-filter-lab.ps1 | ConvertTo-Json -Depth 5 |
        Out-File -LiteralPath (Join-Path $resultsDirectory '05-lab-setup.json') -Encoding utf8
    Write-AppResult '06-crossview-baseline.json' @('--snapshot')
    Write-CommandResult '07-install-filter.log' {
        & .\scripts\install-filter.ps1 `
            -DriverPath '.\out\Debug\x64\RootkitLabFilter.sys' `
            -StartType demand
    }
    Write-AppResult '08-status-initial.json' @('--status')
    Write-AppResult '09-set-rules.json' @(
        '--set-rules', 'proyecto_confidencial.txt', 'presupuesto_2026.xlsx')
    Write-AppResult '10-enable.json' @('--enable')
    [ordered]@{
        entries = @(Get-ChildItem -LiteralPath 'C:\RootkitLabSandbox' -Force |
            Select-Object -ExpandProperty Name)
        selected_direct_read = [string](Get-Content -LiteralPath `
            'C:\RootkitLabSandbox\proyecto_confidencial.txt' -Raw)
    } | ConvertTo-Json -Depth 4 |
        Out-File -LiteralPath (Join-Path $resultsDirectory '11-win32-filtered-view.json') -Encoding utf8
    Write-AppResult '12-crossview-active.json' @('--snapshot')
    [ordered]@{
        outside_entries = @(Get-ChildItem -LiteralPath 'C:\RootkitLabOutside' -Force |
            Select-Object -ExpandProperty Name)
        same_name_outside_visible = Test-Path -LiteralPath `
            'C:\RootkitLabOutside\proyecto_confidencial.txt'
    } | ConvertTo-Json -Depth 4 |
        Out-File -LiteralPath (Join-Path $resultsDirectory '13-outside-scope.json') -Encoding utf8
    Write-AppResult '14-status-active.json' @('--status')
    Write-AppResult '14b-clear-counters.json' @('--clear')
    Write-ExpectedAppFailure '15-update-while-active.json' @(
        '--set-rules', 'contrato_cliente.pdf')
    Write-AppResult '16-disable.json' @('--disable')
    Write-AppResult '17-crossview-restored.json' @('--snapshot')
    Write-ExpectedAppFailure '18-nonexistent-rule.json' @(
        '--set-rules', 'objeto_inexistente.txt')
    Write-AppResult '19-restore-valid-rule.json' @(
        '--set-rules', 'proyecto_confidencial.txt')

    Move-Item -LiteralPath 'C:\RootkitLabSandbox\.rootkitlab-lab' `
        -Destination 'C:\RootkitLabSandbox\.rootkitlab-lab.disabled'
    try {
        Write-ExpectedAppFailure '20-enable-without-marker.json' @('--enable')
    } finally {
        Move-Item -LiteralPath 'C:\RootkitLabSandbox\.rootkitlab-lab.disabled' `
            -Destination 'C:\RootkitLabSandbox\.rootkitlab-lab'
    }
    Write-AppResult '21-clear-rules.json' @('--set-rules')
    Write-ExpectedAppFailure '22-enable-without-rules.json' @('--enable')

    Get-FileHash -Algorithm SHA256 -LiteralPath @(
        '.\out\Debug\x64\RootkitLabFilter.sys',
        '.\out\Debug\x64\RootkitLab.exe'
    ) | Select-Object Path,Hash | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $resultsDirectory '23-hashes.json') -Encoding utf8
    Write-CommandResult '24-filter-imports.txt' {
        & cmd.exe /d /s /c `
            'call D:\BuildEnv\SetupBuildEnv.cmd amd64 >nul && dumpbin.exe /imports out\Debug\x64\RootkitLabFilter.sys'
    }
    Write-CommandResult '25-app-imports.txt' {
        & cmd.exe /d /s /c `
            'call D:\BuildEnv\SetupBuildEnv.cmd amd64 >nul && dumpbin.exe /imports out\Debug\x64\RootkitLab.exe'
    }

    & .\scripts\remove-filter.ps1 | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $resultsDirectory '26-remove-filter.json') -Encoding utf8
    Write-AppResult '27-crossview-after-unload.json' @('--snapshot')
    & .\scripts\cleanup-filter-lab.ps1 | ConvertTo-Json |
        Out-File -LiteralPath (Join-Path $resultsDirectory '28-lab-cleanup.json') -Encoding utf8
    Get-Date -Format o |
        Out-File -LiteralPath (Join-Path $resultsDirectory 'COMPLETE.txt') -Encoding utf8
} catch {
    $_ | Format-List * -Force |
        Out-File -LiteralPath (Join-Path $resultsDirectory 'FAILED.txt') -Encoding utf8 -Width 4096
    try { & (Join-Path $SourceDirectory 'scripts\remove-filter.ps1') *> $null } catch {}
    exit 1
}

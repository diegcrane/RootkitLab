[CmdletBinding()]
param(
    [string]$EvidenceRoot = 'C:\Users\diego\Desktop\TFM\03_evidencias\v2.0'
)

$ErrorActionPreference = 'Stop'
$runs = @('run-01', 'run-02', 'run-03')
$summary = [ordered]@{
    schema = 'rootkitlab-validation-v2.0'
    generated = (Get-Date).ToString('o')
    evidence_root = $EvidenceRoot
    all_passed = $true
    runs = @()
    persistence = $null
}
$referenceSourceDigest = $null

function Read-JsonFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing evidence: $Path"
    }
    Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Test-NameSet([object[]]$Actual, [string[]]$Expected) {
    $actualNames = @($Actual | ForEach-Object { [string]$_ } | Sort-Object)
    $expectedNames = @($Expected | Sort-Object)
    if ($actualNames.Count -ne $expectedNames.Count) { return $false }
    -not (Compare-Object -ReferenceObject $expectedNames -DifferenceObject $actualNames)
}

foreach ($run in $runs) {
    $path = Join-Path $EvidenceRoot $run
    $context = Read-JsonFile (Join-Path $path '01-context.json')
    $sourceManifest = Read-JsonFile (Join-Path $path '02-source-manifest.json')
    $baseline = Read-JsonFile (Join-Path $path '06-crossview-baseline.json')
    $initial = Read-JsonFile (Join-Path $path '08-status-initial.json')
    $rules = Read-JsonFile (Join-Path $path '09-set-rules.json')
    $enabled = Read-JsonFile (Join-Path $path '10-enable.json')
    $win32 = Read-JsonFile (Join-Path $path '11-win32-filtered-view.json')
    $active = Read-JsonFile (Join-Path $path '12-crossview-active.json')
    $outside = Read-JsonFile (Join-Path $path '13-outside-scope.json')
    $activeStatus = Read-JsonFile (Join-Path $path '14-status-active.json')
    $clearedStatus = Read-JsonFile (Join-Path $path '14b-clear-counters.json')
    $busy = Read-JsonFile (Join-Path $path '15-update-while-active.json')
    $restored = Read-JsonFile (Join-Path $path '17-crossview-restored.json')
    $missing = Read-JsonFile (Join-Path $path '18-nonexistent-rule.json')
    $noMarker = Read-JsonFile (Join-Path $path '20-enable-without-marker.json')
    $noRules = Read-JsonFile (Join-Path $path '22-enable-without-rules.json')
    $binaryHashes = @(Read-JsonFile (Join-Path $path '23-hashes.json'))
    $afterUnload = Read-JsonFile (Join-Path $path '27-crossview-after-unload.json')
    $cleanup = Read-JsonFile (Join-Path $path '28-lab-cleanup.json')

    $binaryMap = [ordered]@{}
    foreach ($entry in $binaryHashes) {
        $binaryMap[(Split-Path -Leaf $entry.Path)] = [string]$entry.Hash
    }
    if ($null -eq $referenceSourceDigest) {
        $referenceSourceDigest = [string]$sourceManifest.tree_sha256
    }

    $expectedRules = @('proyecto_confidencial.txt', 'presupuesto_2026.xlsx')
    $checks = [ordered]@{
        complete_marker = Test-Path -LiteralPath (Join-Path $path 'COMPLETE.txt')
        no_failure_marker = -not (Test-Path -LiteralPath (Join-Path $path 'FAILED.txt'))
        isolated_vm = [int]$context.network_adapters -eq 0
        source_tree_matches = [string]$sourceManifest.tree_sha256 -eq $referenceSourceDigest
        baseline_consistent = $baseline.classification -eq 'consistent' -and [int]$baseline.missing_count -eq 0
        independent_sources = [bool]$active.sources_independent
        safe_initial_state = -not [bool]$initial.enabled -and [int]$initial.rules.Count -eq 0
        exact_rules_applied = Test-NameSet @($rules.rules) $expectedRules
        enabled_with_marker = [bool]$enabled.enabled -and [bool]$enabled.marker_present
        selected_absent_from_win32 = `
            'proyecto_confidencial.txt' -notin @($win32.entries) -and `
            'presupuesto_2026.xlsx' -notin @($win32.entries)
        direct_read_preserved = [string]$win32.selected_direct_read -match 'Internal project notes'
        active_detected = $active.classification -eq 'cross_view_inconsistency' -and [int]$active.missing_count -eq 2
        exact_names_detected = Test-NameSet @($active.missing_from_win32) $expectedRules
        direct_open_preserved = [bool]$active.direct_open_ok
        outside_scope_visible = [bool]$outside.same_name_outside_visible
        counters_observed = [bool]$activeStatus.enabled -and `
            [long]$activeStatus.counters.hidden_entries -ge 2 -and `
            @($clearedStatus.counters.PSObject.Properties | `
                Where-Object { [long]$_.Value -ne 0 }).Count -eq 0
        update_while_active_rejected = [bool]$busy.passed
        restored_consistent = $restored.classification -eq 'consistent' -and [int]$restored.missing_count -eq 0
        nonexistent_rule_rejected = [bool]$missing.passed
        marker_guard_rejected = [bool]$noMarker.passed
        empty_rule_guard_rejected = [bool]$noRules.passed
        exactly_two_artifacts = $binaryMap.Count -eq 2 -and `
            $binaryMap.Contains('RootkitLab.exe') -and `
            $binaryMap.Contains('RootkitLabFilter.sys')
        binary_hashes_valid = @($binaryMap.Values | Where-Object { $_ -notmatch '^[0-9A-F]{64}$' }).Count -eq 0
        unload_restores_view = $afterUnload.classification -eq 'consistent' -and [int]$afterUnload.missing_count -eq 0
        cleanup_complete = -not [bool]$cleanup.SandboxPresent -and -not [bool]$cleanup.OutsideControlPresent
    }
    $passed = @($checks.Values | Where-Object { -not $_ }).Count -eq 0
    if (-not $passed) { $summary.all_passed = $false }
    $summary.runs += [ordered]@{
        run = $run
        passed = $passed
        checks = $checks
        source_tree_sha256 = $sourceManifest.tree_sha256
        binary_hashes = $binaryMap
    }
}

$persistencePath = Join-Path $EvidenceRoot 'persistence'
$before = Read-JsonFile (Join-Path $persistencePath 'PENDING-REBOOT.json')
$after = Read-JsonFile (Join-Path $persistencePath '09-context-after-reboot.json')
$status = Read-JsonFile (Join-Path $persistencePath '12-status-after-reboot.json')
$safeView = Read-JsonFile (Join-Path $persistencePath '13-crossview-disabled-after-reboot.json')
$activeAfterReboot = Read-JsonFile (Join-Path $persistencePath '16-crossview-active-after-reboot.json')
$cleanup = Read-JsonFile (Join-Path $persistencePath '20-cleanup.json')
$serviceText = Get-Content -LiteralPath (Join-Path $persistencePath '10-service-after-reboot.txt') -Raw
$filterText = Get-Content -LiteralPath (Join-Path $persistencePath '11-filters-after-reboot.txt') -Raw

$persistenceChecks = [ordered]@{
    complete_marker = Test-Path -LiteralPath (Join-Path $persistencePath 'COMPLETE.txt')
    no_failure_marker = `
        -not (Test-Path -LiteralPath (Join-Path $persistencePath 'FAILED-PRE.txt')) -and `
        -not (Test-Path -LiteralPath (Join-Path $persistencePath 'FAILED-POST.txt'))
    reboot_occurred = $before.boot_time_before -ne $after.boot_time_after
    system_start_before = [int]$before.service_start -eq 1
    system_start_after = [int]$after.service_start -eq 1
    service_running_after_reboot = $serviceText -match 'STATE\s+: 4\s+RUNNING'
    filter_attached_after_reboot = $filterText -match 'RootkitLabFilter\s+1\s+370030'
    safe_default_after_reboot = -not [bool]$status.enabled -and [int]$status.rules.Count -eq 0
    marker_preserved = [bool]$status.marker_present
    safe_view_after_reboot = $safeView.classification -eq 'consistent' -and [int]$safeView.missing_count -eq 0
    effect_operational_after_reboot = `
        $activeAfterReboot.classification -eq 'cross_view_inconsistency' -and `
        [int]$activeAfterReboot.missing_count -eq 2
    cleanup_complete = -not [bool]$cleanup.SandboxPresent -and -not [bool]$cleanup.OutsideControlPresent
}
$persistencePassed = @($persistenceChecks.Values | Where-Object { -not $_ }).Count -eq 0
if (-not $persistencePassed) { $summary.all_passed = $false }
$summary.persistence = [ordered]@{
    passed = $persistencePassed
    checks = $persistenceChecks
    boot_time_before = $before.boot_time_before
    boot_time_after = $after.boot_time_after
}

$output = Join-Path $EvidenceRoot 'VALIDATION-SUMMARY.json'
$summary | ConvertTo-Json -Depth 8 | Out-File -LiteralPath $output -Encoding utf8
Get-Content -LiteralPath $output -Raw
if (-not $summary.all_passed) { exit 1 }

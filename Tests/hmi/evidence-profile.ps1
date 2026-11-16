$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=Join-Path $taskRoot 'tmp/evidence-profile-test'
& (Join-Path $taskRoot 'Tools/Hmi/Build-BoardProfile.ps1') -Config (Join-Path $taskRoot 'Assets/board.json') -OutputDirectory $taskOut
$taskConfig=Get-Content (Join-Path $taskRoot 'Assets/board.json') -Raw|ConvertFrom-Json
$taskReady=Get-Content (Join-Path $taskOut 'readiness.json') -Raw|ConvertFrom-Json
$taskDetail=Get-Content (Join-Path $taskOut 'readiness-detail.json') -Raw|ConvertFrom-Json
$taskGenerated=Get-Content (Join-Path $taskOut 'board-profile.c') -Raw
if($taskConfig.burner.damper_timeout_ms -ne $taskConfig.actuation.damper_timeout_ms){throw 'Process/actuator timeout mismatch'}
if($taskConfig.burner.input_max_age_ms -ne 500){throw 'Reconciled analog age mismatch'}
foreach($taskItem in @('burner.damper_timeout_ms','burner.input_max_age_ms','hardware.damper_reference_input')) {
 if($taskReady.missing -contains $taskItem){throw ('Resolved item still listed: '+$taskItem)}
}
if($taskDetail.missing_by_kind.optional -notcontains 'calibration.mixer_ntc'){throw 'Optional mixer incorrectly classified'}
if($taskDetail.missing_by_kind.integration -notcontains 'hardware.real_safety_input_acquisition'){throw 'Wiring gap incorrectly classified'}
if($taskDetail.missing_by_kind.design -notcontains 'burner.co_accept_ppm'){throw 'CO design criterion incorrectly classified'}
if($taskDetail.missing_by_kind.measurement -notcontains 'calibration.co.zero_uv'){throw 'CO calibration incorrectly classified'}
if($taskReady.production_ready -or !$taskReady.actuation_inhibited -or $taskGenerated -notmatch '\.commissioned=false'){throw 'Evidence reconciliation enabled combustion'}
Write-Output 'PASS evidence profile: resolved settings, issue classification and commissioning inhibit.'

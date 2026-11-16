param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe',
 [string]$OutputDirectory=(Join-Path $PSScriptRoot '../../Debug/Scenario'))
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($taskOut)|Out-Null
$taskBoard=Get-Content -LiteralPath (Join-Path $taskRoot 'Assets/board.json') -Raw|ConvertFrom-Json
$taskTimings=[ordered]@{purge_ms=[uint32]$taskBoard.burner.purge_ms;ignition_ms=[uint32]$taskBoard.burner.ignition_ms;postpurge_ms=[uint32]$taskBoard.burner.postpurge_ms}
if($taskTimings.purge_ms -ne 30000 -or $taskTimings.ignition_ms -ne 5000 -or $taskTimings.postpurge_ms -ne 60000){throw 'Scenario expectations need review after user timing changes.'}
$taskHeader=@('#define SCENARIO_PURGE_MS 30000U','#define SCENARIO_IGNITION_MS 5000U','#define SCENARIO_POSTPURGE_MS 60000U')
[IO.File]::WriteAllLines((Join-Path $taskOut 'scenario-profile.h'),$taskHeader)
$taskExe=Join-Path $taskOut 'scenario-test.exe'
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Core/Inc') -I $taskOut `
 (Join-Path $taskRoot 'Core/Src/BurnerControl.c') (Join-Path $taskRoot 'Core/Src/CombustionCycle.c') (Join-Path $PSScriptRoot 'scenario.c') -o $taskExe
if($LASTEXITCODE -ne 0){throw 'Scenario compilation failed'}
$taskCases=@(& $taskExe $taskOut)
if($LASTEXITCODE -ne 0){throw ('Scenario verification failed: '+($taskCases -join '; '))}
$taskResults=@($taskCases|ForEach-Object {$_|ConvertFrom-Json})
if($taskResults.Count -lt 10 -or @($taskResults|Where-Object {!$_.passed}).Count){throw 'Incomplete/failed scenario results'}
$taskReport=[ordered]@{schema=1;scope='HOST_SIMULATION_OF_ACTUAL_CONTROL_ENGINE';physical_outputs_connected=$false;board_programmed=$false;
 requested_timings=$taskTimings;assumptions=[ordered]@{interpurge_ms=30000;flame_stable_ms=500;airflow_feedback_delay_ms=100;fan_speed_feedback='synthetic, not calibrated';co_values='synthetic fixture ppm, NOT a sensor calibration or operating threshold';co_accept_ppm=5;co_limit_ppm=10;co_trip_ppm=50;air_initial_percent=50;air_settle_ms=30000;cooling_timeout_ms=180000;max_adjustments=3};
 cases=$taskResults;all_passed=$true;production_ready=$false;note='No HAL, GPIO, SPI, programmer or physical fuel control is linked. Inputs/feedback are explicitly synthetic.'}
[IO.File]::WriteAllText((Join-Path $taskOut 'scenario-report.json'),($taskReport|ConvertTo-Json -Depth 10))
$taskLines=@('SCENARIO VERIFICATION — HOST SIMULATION',('Passed cases: '+$taskResults.Count),'Actual BurnerControl.c + CombustionCycle.c; synthetic input fixtures; no board connection.','User timings: purge30s / ignition5s / postpurge60s.','Interpurge30s and flame stability500ms are test assumptions, not validated burner settings.','CO ppm/limits and endpoint feedback are fixture data only.','')
$taskLines+=@($taskResults|ForEach-Object {'PASS '+$_.name+' (virtual '+$_.virtual_ms+'ms)'})
[IO.File]::WriteAllLines((Join-Path $taskOut 'scenario-summary.txt'),$taskLines)
Write-Host ('PASS '+$taskResults.Count+' end-to-end virtual scenarios; report: '+(Join-Path $taskOut 'scenario-report.json'))

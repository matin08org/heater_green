param([Parameter(Mandatory=$true)][string]$Config,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskProfile=Get-Content -LiteralPath $Config -Raw|ConvertFrom-Json
if($taskProfile.schema -ne 1){throw 'Unsupported board profile schema.'}
if($taskProfile.scenario_test.enabled -or $taskProfile.output_test.enabled) {
 throw 'The application uses real inputs only. Runtime simulation and dummy-load output tests have been removed; use the standalone host tests.'
}
$taskOut=[IO.Path]::GetFullPath($OutputDirectory);[IO.Directory]::CreateDirectory($taskOut)|Out-Null
$taskMissing=[Collections.Generic.List[string]]::new()
$taskLines=[Collections.Generic.List[string]]::new();$taskLines.Add('#include "BoardRuntime.h"');$taskLines.Add('#include "SensorMath.h"');$taskLines.Add('#include "BoardBus.h"')
$taskLines.Add('const BurnerConfig board_burner_profile = { .commissioned=false, .adaptive_air_enabled=true, .cooling_mode=COOL_BY_TEMPERATURE,')
foreach($p in $taskProfile.burner.PSObject.Properties) {
 if($p.Name -notmatch '^[a-z][a-z0-9_]*$'){throw 'Invalid configuration key.'}
 $value=$p.Value;if($null -eq $value){$taskMissing.Add('burner.'+$p.Name);$value=0}
 if($value -lt 0 -or $value -ge [int]::MaxValue -or $value -ne [long]$value){throw ('Invalid integer setting: '+$p.Name)}
 $taskLines.Add((' .{0}={1}U,' -f $p.Name,$value))
}
$taskLines.Add('};')

 $a=$taskProfile.actuation
 if(!$a){throw 'Missing actuation configuration.'}
 $ccr=@($a.fan_ccr_by_10_percent)
 if($a.fan_calibrated) {
  if($ccr.Count -ne 11 -or $ccr[0] -ne 1000){throw 'Fan CCR table needs 11 measured entries; index 0 must be 1000 (OFF).'}
  for($i=1;$i -lt 11;$i++) {
   if($null -eq $ccr[$i] -or $ccr[$i] -lt 0 -or $ccr[$i] -gt 333 -or $ccr[$i] -gt $ccr[$i-1] -or $ccr[$i] -ne [int]$ccr[$i]){throw 'Invalid measured fan CCR table.'}
  }
 } else {$ccr=@(1000)*11;$taskMissing.Add('actuation.measured_fan_ccr_table')}
 if($a.damper_travel_pulses -lt 1 -or $a.damper_travel_pulses -gt 100000 -or $a.damper_timeout_ms -lt 1000 -or $a.damper_timeout_ms -gt 120000 -or $a.hall_debounce_ms -lt 5 -or $a.hall_debounce_ms -gt 100 -or $a.damper_rpm -lt 1 -or $a.damper_rpm -gt 120){throw 'Invalid bounded damper settings.'}
 $taskLines.Add('#include "BoardActuation.h"')
 if($null -eq $a.endpoint_tolerance_pulses -or $a.endpoint_tolerance_pulses -lt 0 -or $a.endpoint_tolerance_pulses -gt 64 -or $a.endpoint_tolerance_pulses*2 -ge $a.damper_travel_pulses){throw 'Invalid Hall endpoint tolerance.'}
 $taskLines.Add(('const BoardActuationConfig board_actuation_config={{.fan_calibrated={0},.fan_ccr={{{1}}},.travel_pulses={2}U,.travel_timeout_ms={3}U,.hall_debounce_ms={4}U,.rpm={5}.0f,.endpoint_tolerance_pulses={6}U}};' -f ([string][bool]$a.fan_calibrated).ToLowerInvariant(),($ccr -join ','),[int]$a.damper_travel_pulses,[int]$a.damper_timeout_ms,[int]$a.hall_debounce_ms,[int]$a.damper_rpm,[int]$a.endpoint_tolerance_pulses))
if($taskProfile.rs485) {
 $r=$taskProfile.rs485
 if($r.baud -notin @(9600,19200,38400,57600,115200) -or $r.parity -notin @(0,1,2) -or $r.stop_bits -notin @(1,2)) {throw 'Invalid serial settings.'}
 if($r.quiet_ms -lt [Math]::Max(2,[Math]::Ceiling(38500.0/$r.baud)) -or $r.timeout_ms -lt 10 -or $r.poll_ms -lt 10 -or $r.retries -gt 3 -or $r.retries -lt 0) {throw 'Invalid bounded RTU timing/retry configuration.'}
 if($r.plc_id -lt 1 -or $r.plc_id -gt 247 -or $r.lux_id -lt 1 -or $r.lux_id -gt 247 -or $r.debug_id -lt 1 -or $r.debug_id -gt 247 -or @($r.plc_id,$r.lux_id,$r.debug_id|Select-Object -Unique).Count -ne 3) {throw 'RS485 node IDs must be distinct, 1..247.'}
 if($r.plc_command_register -gt 65532 -or $r.plc_status_register -gt 65527 -or $r.lux_register -gt 65534){throw 'Register range exceeds uint16 address space.'}
 if($r.lux_day_on_centi -le $r.lux_day_off_centi -or $r.lux_stale_ms -lt $r.poll_ms*4) {throw 'Invalid lux hysteresis/stale configuration.'}
 if($r.de_port -and $r.de_port -notmatch '^[A-D]$') {throw 'DE port must be A..D or null.'}
 if($null -ne $r.de_pin -and ($r.de_pin -lt 0 -or $r.de_pin -gt 15)) {throw 'DE pin must be 0..15 or null.'}
 if($r.de_port -and $null -ne $r.de_pin -and ($r.de_port+[string]$r.de_pin) -notin @('B4','B11','C15')) {throw 'Selected DE pin conflicts with an existing signal or is not available in the current 64-pin board map. Update and verify the pin map before assigning it.'}
 if($r.enabled -and (!$r.de_port -or $null -eq $r.de_pin -or !$r.direction_confirmed)) {throw 'Enabling RS485 master requires actual DE pin and confirmed tied DE/RE direction wiring.'}
 $port=if($r.de_port){"'"+$r.de_port+"'"}else{'0'};$pin=if($null -eq $r.de_pin){255}else{[int]$r.de_pin}
 $taskLines.Add(('const BoardBusConfig board_bus_config = {{ .enabled={0},.debug_enabled={1},.de_port={2},.de_pin={3},.direction_confirmed={4},' -f ([string][bool]$r.enabled).ToLowerInvariant(),([string][bool]$r.debug_enabled).ToLowerInvariant(),$port,$pin,([string][bool]$r.direction_confirmed).ToLowerInvariant()))
 foreach($key in @('baud','parity','stop_bits','plc_id','lux_id','debug_id','plc_command_register','plc_status_register','lux_register','poll_ms','timeout_ms','quiet_ms','retries','lux_stale_ms','lux_day_on_centi','lux_day_off_centi')) {
  $v=$r.$key;if($null -eq $v -or $v -lt 0 -or $v -ge [int]::MaxValue -or $v -ne [long]$v){throw ('Invalid RS485 value: '+$key)}
  $taskLines.Add((' .{0}={1}U,' -f $key,$v))
 }
 $taskLines.Add('};')
 if(!$r.enabled -or !$r.direction_confirmed){$taskMissing.Add('rs485.direction_and_enable')}
} else {throw 'Missing rs485 profile.'}
function FloatValue($v) {if($null -eq $v){return '0.0f'};$s=([double]$v).ToString('R',[Globalization.CultureInfo]::InvariantCulture);if($s -notmatch '[.Ee]'){$s+='.0'};return $s+'f'}
$co=$taskProfile.calibration.co
$taskLines.Add(('const BoardAnalogCalibration board_analog_calibration = {{ .calibrated={0},.zero_uv={1},.sensitivity_uv_per_ppm={2},.reference_c={3},.span_temp_coefficient={4},' -f ([string][bool]$co.calibrated).ToLowerInvariant(),(FloatValue $co.zero_uv),(FloatValue $co.sensitivity_uv_per_ppm),(FloatValue $co.reference_c),(FloatValue $co.span_temp_coefficient)))
foreach($name in @('me2_ntc','mixer_ntc')) {
 $n=$taskProfile.calibration.$name
 $taskLines.Add((' .{0}={{.calibrated={1},.pullup_ohms={2},.r25_ohms={3},.beta={4}}},' -f $name,([string][bool]$n.calibrated).ToLowerInvariant(),(FloatValue $n.pullup_ohms),(FloatValue $n.r25_ohms),(FloatValue $n.beta)))
}
$taskLines.Add('};')
foreach($name in @('co','me2_ntc','mixer_ntc')) {
 $n=$taskProfile.calibration.$name
 if(!$n.calibrated){$taskMissing.Add('calibration.'+$name)}
 foreach($p in $n.PSObject.Properties){if($null -eq $p.Value){$taskMissing.Add('calibration.'+$name+'.'+$p.Name)}}
}
foreach($p in $taskProfile.hardware.PSObject.Properties){if($null -eq $p.Value){$taskMissing.Add('hardware.'+$p.Name)}}
if(!$taskProfile.safety_approved){$taskMissing.Add('safety_approved')}
if(!$taskProfile.board_tests_passed){$taskMissing.Add('board_tests_passed')}
# The connected actuation adapter still needs board validation and real safety
# input acquisition. A JSON approval flag is not that validation.
$taskMissing.Add('validated_actuation_adapter')
$taskMissing.Add('hardware.real_safety_input_acquisition')
# Keep the legacy aggregate for consumers, but distinguish measurements from
# unavailable wiring, design choices and optional features. None grants approval.
$taskKinds=[ordered]@{measurement=@();integration=@();design=@();validation=@();optional=@()}
foreach($item in $taskMissing) {
 $kind=switch -Regex ($item) {
  '^calibration\.mixer_ntc' {'optional';break}
  '^(rs485\.|hardware\.rs485_de_re)' {'integration';break}
  '^hardware\.(airflow_proof_input|detector_health_input|fuel_off_proof_input|ignition_off_proof_input|fan_stopped_proof_input|real_safety_input_acquisition|independent_fuel_cutoff)$' {'integration';break}
  '^burner\.(required_purge_ms|co_limit_ppm|co_accept_ppm|co_trip_ppm|max_adjustments|air_initial_percent)$' {'design';break}
  '^(safety_approved|board_tests_passed|validated_actuation_adapter)$' {'validation';break}
  default {'measurement'}
 }
 $taskKinds[$kind]+=$item
}
if($taskProfile.safety_approved){throw 'A JSON flag cannot commission the safety inputs/actuation adapter. Complete and validate board integration first.'}
$taskLines.Add('const char *const board_pending_items[] = {')
foreach($item in $taskMissing){$taskLines.Add(' "'+$item+'",')}
$taskLines.Add('};');$taskLines.Add(('const uint16_t board_pending_count={0};' -f $taskMissing.Count))
[IO.File]::WriteAllLines((Join-Path $taskOut 'board-profile.c'),$taskLines)
[IO.File]::WriteAllText((Join-Path $taskOut 'readiness-detail.json'),([ordered]@{schema=1;evidence='Assets/Evidence/review.json';missing_by_kind=$taskKinds;notes=@('RS485 is not required for local MANUAL, but the current AUTO daylight source requires it.','Mixer NTC is optional; TC2 controls sample cooling.','Design requirements cannot be established by a measurement alone.','No safety input or calibration is replaced with a synthetic value.')}|ConvertTo-Json -Depth 8))
[IO.File]::WriteAllText((Join-Path $taskOut 'readiness.json'),([ordered]@{production_ready=$false;build_class='INTEGRATED_ENGINEERING';operation_mode='PHYSICAL_INHIBITED';missing=@($taskMissing);actuation_inhibited=$true;actuation_inhibited_at_boot=$true;physical_test_capable=[bool]$taskProfile.output_test.enabled;output_test_requires_operator_arm=$true}|ConvertTo-Json -Depth 8))
Write-Output ('Board profile: integrated engineering build; '+$taskMissing.Count+' commissioning/integration items remain.')

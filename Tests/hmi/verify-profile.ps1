param(
 [string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe',
 [string]$Tools='C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin',
 [string]$ProfileDirectory,[switch]$FinalApplication
)
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=if($ProfileDirectory){[IO.Path]::GetFullPath($ProfileDirectory)}else{Join-Path $taskRoot 'Debug/HmiMonitor'}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Core/Inc') (Join-Path $taskRoot 'Tests/assets/test.c') -o (Join-Path $taskOut 'asset-check.exe')
if($LASTEXITCODE -ne 0) {throw 'Asset checker compile failed'}
& (Join-Path $taskOut 'asset-check.exe') (Join-Path $taskOut 'External_Assets.bin') (Join-Path $taskOut 'expected-crc.bin')
if($LASTEXITCODE -ne 0) {throw 'Actual monitor asset CRC check failed'}
$taskCpu=[IO.File]::ReadAllBytes((Join-Path $taskOut 'Internal_CPU.bin'))
$taskSp=[BitConverter]::ToUInt32($taskCpu,0);$taskReset=[BitConverter]::ToUInt32($taskCpu,4)
if($taskSp -ne 0x20040000 -or $taskReset -lt 0x08000001 -or $taskReset -ge 0x08080000 -or !($taskReset -band 1)) {throw 'Bad internal reset vectors'}
$taskSymbols=& (Join-Path $Tools 'arm-none-eabi-nm.exe') --defined-only (Join-Path $taskOut 'HmiMonitor.elf')
if($LASTEXITCODE -ne 0) {throw 'Symbol inspection failed'}
$taskTable=@{}
foreach($taskLine in $taskSymbols) {
 if($taskLine -match '^([0-9a-fA-F]+)\s+([A-Za-z])\s+(.+)$') {
  $taskAddress=[Convert]::ToUInt32($Matches[1],16);$taskType=$Matches[2];$taskName=$Matches[3]
  $taskTable[$taskName]=$taskAddress
  if($taskType -cmatch '^[Tt]$' -and ($taskAddress -lt 0x08000000 -or $taskAddress -ge 0x08080000)) {throw ('Executable outside internal flash: '+$taskLine)}
  if($taskName.StartsWith('lv_')) {throw ('Unexpected LVGL symbol: '+$taskName)}
 }
}
foreach($taskName in @('main','App_Process','MAX6675_Process','NRF24_Process','HmiMonitor_KeyTick','HmiDiag_Compose','HmiMonitorIo_Init','DMA2_Stream0_IRQHandler','DMA2_Stream5_IRQHandler','HardFault_Handler')) {
 if(!$taskTable.ContainsKey($taskName)) {throw ('Missing integration symbol: '+$taskName)}
}
if($taskTable['monitor_font_data'] -lt 0x90000100L -or $taskTable['monitor_font_data'] -ge 0x90200000L) {throw 'External font not in QSPI'}
if($taskTable['monitor_fallback_font_data'] -lt 0x08000000 -or $taskTable['monitor_fallback_font_data'] -ge 0x08080000) {throw 'Fallback font not internal'}
if($FinalApplication) {
 foreach($taskName in $taskTable.Keys) {
  if($taskName -match '^(BoardScenario_|BoardOutputTest_|BoardActuation_ApplyOutputTest$)') {
   throw ('Test-only executable linked into real-input firmware: '+$taskName)
  }
 }
 foreach($taskName in @('Burner_Process','BurnerApp_Process','BurnerApp_Start','CombustionCycle_Process','SensorMath_Convert','Rtu_Process','BoardBus_Process','HmiPagesDma_Compose','HmiAssetDma_IRQHandler','DMA2_Stream1_IRQHandler','UiNavigation_Key','TIM1_PhaseSetCompare','TIM4_IRQHandler','Buzzer_TimerIRQ')) {
  if(!$taskTable.ContainsKey($taskName)){throw ('Missing final application component: '+$taskName)}
 }
 if($taskTable['hmi_pages'] -lt 0x90000100L -or $taskTable['hmi_pages'] -ge 0x90200000L){throw 'Page descriptors must be external assets.'}
 foreach($taskName in @('home_small_font_data','home_value_font_data')) {
  if($taskTable.ContainsKey($taskName) -and ($taskTable[$taskName] -lt 0x90000100L -or $taskTable[$taskName] -ge 0x90200000L)){throw 'Home font pixels must be in external assets.'}
 }
}
$taskCrcOffset=[int64]$taskTable['asset_expected_crc']-0x08000000
$taskExpected=[IO.File]::ReadAllBytes((Join-Path $taskOut 'expected-crc.bin'))
if([BitConverter]::ToUInt32($taskCpu,$taskCrcOffset) -ne [BitConverter]::ToUInt32($taskExpected,0)) {throw 'CPU expectation does not match exported assets'}
Write-Output 'PASS profile: internal vectors/all code/fallback font, external asset font, actual CRC and CPU expectation match, ADC/UART/key integration present, no LVGL.'

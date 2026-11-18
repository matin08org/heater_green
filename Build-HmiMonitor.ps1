param([string]$Tools='C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin',
 [switch]$FinalApplication,[string]$HomePng,[string]$UiConfig,[string]$BoardConfig)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskOut=Join-Path $taskRoot $(if($FinalApplication){'Debug/FinalV1'}else{'Debug/HmiMonitor'})
New-Item -ItemType Directory -Force $taskOut | Out-Null
$taskGcc=Join-Path $Tools 'arm-none-eabi-gcc.exe'
$taskCopy=Join-Path $Tools 'arm-none-eabi-objcopy.exe'
& (Join-Path $taskRoot 'Tools/Hmi/Export-Font.ps1') -OutputBase (Join-Path $taskOut 'font') -PixelSize 14 -Symbol monitor_font -Storage External
& (Join-Path $taskRoot 'Tools/Hmi/Export-Font.ps1') -OutputBase (Join-Path $taskOut 'fallback') -PixelSize 14 -Symbol monitor_fallback_font
$taskFlags=@('-mcpu=cortex-m4','-mthumb','-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-std=gnu11','-O2','-g3','-Wall','-Wextra','-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-fstack-usage','-DUSE_HAL_DRIVER','-DSTM32F412Rx','-DFW_INTERNAL_FLASH_APP=1','-DFW_CUSTOM_HMI=1','-DHSE_VALUE=12000000U')
if($FinalApplication) {
 $taskFlags+='-DFW_FINAL_APPLICATION=1'
 & (Join-Path $taskRoot 'Tools/Hmi/Export-Font.ps1') -OutputBase (Join-Path $taskOut 'font-small') -FontFamily 'Segoe UI' -PixelSize 11 -Symbol home_small_font -Storage External -CoverageBits 2 -Proportional
 & (Join-Path $taskRoot 'Tools/Hmi/Export-Font.ps1') -OutputBase (Join-Path $taskOut 'font-value') -FontFamily 'Segoe UI' -PixelSize 28 -FirstChar 32 -LastChar 63 -Symbol home_value_font -Storage External -CoverageBits 2 -Proportional -Bold -TrimVertical
 if(!$UiConfig){$UiConfig=Join-Path $taskRoot 'Assets/ui.json'}
 if(!$BoardConfig){$BoardConfig=Join-Path $taskRoot 'Assets/board.json'}
 & (Join-Path $taskRoot 'Tools/Hmi/Build-Pages.ps1') -Config $UiConfig -OutputDirectory $taskOut -HomePng $HomePng
 & (Join-Path $taskRoot 'Tools/Hmi/Build-BoardProfile.ps1') -Config $BoardConfig -OutputDirectory $taskOut
}
foreach($taskInclude in @('Core/Inc','Libraries/Hmi/include','Libraries/Hmi/platform','Drivers/STM32F4xx_HAL_Driver/Inc','Drivers/STM32F4xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Device/ST/STM32F4xx/Include','Drivers/CMSIS/Include')) {
 $taskFlags+=@('-I',(Join-Path $taskRoot $taskInclude))
}
$taskSources=@('HmiMonitorMain','HmiMonitorIo','HmiMonitorIrq','HmiMonitorFault','App','Buzzer','DamperStepper','HC595','MAX6675','NRF24L01','DiagLog','KeyInput','InternalAssets','adc','dma','gpio','spi','stm32f4xx_hal_msp','syscalls','sysmem','system_stm32f4xx','tim','usart')|ForEach-Object {'Core/Src/'+$_+'.c'}
$taskSources+=@('HmiTypes','HmiImage','HmiText','HmiStream','HmiDiag','HmiDiagData')|ForEach-Object {'Libraries/Hmi/src/'+$_+'.c'}
$taskSources+='Libraries/Hmi/platform/HmiSt7796.c'
$taskSources+='Core/Startup/startup_stm32f412retx.s'
if($FinalApplication) {
 $taskSources+=@('Core/Src/BoardActuation.c','Core/Src/BurnerRuntime.c','Core/Src/StartOverride.c','Core/Src/BurnerControl.c','Core/Src/CombustionCycle.c','Core/Src/SensorMath.c','Core/Src/UiNavigation.c','Core/Src/RtuMaster.c','Core/Src/BoardBus.c','Libraries/Hmi/src/HmiPages.c','Libraries/Hmi/src/HmiHome.c',
    'Libraries/Hmi/src/HmiPagesDma.c','Libraries/Hmi/platform/HmiAssetDma.c',
    (Join-Path $taskOut 'pages.c'),(Join-Path $taskOut 'board-profile.c'),(Join-Path $taskOut 'font-small.c'),(Join-Path $taskOut 'font-value.c'))
}
foreach($taskLine in Get-Content (Join-Path $taskRoot 'Tools/Hmi/hal-sources.txt')) {
 if($taskLine.Trim()){$taskSources+='Drivers/STM32F4xx_HAL_Driver/Src/'+$taskLine.Trim()+'.c'}
}
$taskSources+=@((Join-Path $taskOut 'font.c'),(Join-Path $taskOut 'fallback.c'))
$taskObjects=@()
foreach($taskSource in $taskSources) {
 $taskFull=if([IO.Path]::IsPathRooted($taskSource)){$taskSource}else{Join-Path $taskRoot $taskSource}
 $taskObject=Join-Path $taskOut (([IO.Path]::GetFileNameWithoutExtension($taskSource))+'.o')
 $taskArgs=@($taskFlags)
 if($taskSource -match 'Hmi|BurnerRuntime|StartOverride|SensorMath|CombustionCycle|RtuMaster|BoardBus|board-profile|pages.c') {$taskArgs+='-Werror'}
 & $taskGcc @taskArgs -c $taskFull -o $taskObject
 if($LASTEXITCODE -ne 0) {throw ('Compile failed: '+$taskSource)}
 $taskObjects+=$taskObject
}
$taskLink=[IO.File]::ReadAllText((Join-Path $taskRoot 'STM32F412RETX_FLASH.ld'))
$taskLink=$taskLink.Replace('ORIGIN = 0x90000000,   LENGTH = 2M','ORIGIN = 0x08000000,   LENGTH = 512K').Replace('_Min_Stack_Size = 0x400','_Min_Stack_Size = 0x2000')
$taskAt=$taskLink.IndexOf('/* Sections */');$taskMemEnd=$taskLink.LastIndexOf('}',$taskAt)
$taskLink=$taskLink.Insert($taskMemEnd,"  ASSETS (r) : ORIGIN = 0x90000000, LENGTH = 2M`n")
$taskSections=@'
  .noinit (NOLOAD) : { *(.noinit*) } >RAM
  .assets_header (READONLY) : {
    LONG(0x41535434); LONG(__assets_end - __assets_start); LONG(0); LONG(1);
  } >ASSETS
  .assets ORIGIN(ASSETS)+0x100 (READONLY) : {
    __assets_start = .;
    *(.hmi_assets)
    . = ALIGN(4); __assets_end = .;
  } >ASSETS
  .asset_expectation (READONLY) : { asset_expected_crc = .; LONG(0); } >FLASH

'@
$taskLink=$taskLink.Replace('  /* Constant data into "FLASH" Rom type memory */',$taskSections+'  /* Constant data into "FLASH" Rom type memory */')
$taskLink+="`nASSERT(ADDR(.isr_vector)==0x08000000,`"internal vectors required`")`nASSERT(ADDR(.text)+SIZEOF(.text)<=0x08080000,`"internal code overflow`")`nASSERT(LOADADDR(.data)+SIZEOF(.data)<=0x08080000,`"internal data load overflow`")`n"
$taskLd=Join-Path $taskOut 'HmiMonitor.ld';[IO.File]::WriteAllText($taskLd,$taskLink)
$taskRsp=Join-Path $taskOut 'objects.rsp';[IO.File]::WriteAllLines($taskRsp,@($taskObjects|ForEach-Object {'"'+$_.Replace('\','/')+'"'}))
$taskLinked=Join-Path $taskOut 'linked.elf'
& $taskGcc ('@'+$taskRsp) -o $taskLinked -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard ('-T'+$taskLd) --specs=nosys.specs --specs=nano.specs -static '-Wl,--gc-sections' ('-Wl,-Map='+(Join-Path $taskOut 'HmiMonitor.map')) '-Wl,--start-group' -lc -lm '-Wl,--end-group'
if($LASTEXITCODE -ne 0) {throw 'Monitor link failed'}
$taskBody=Join-Path $taskOut 'assets-body.bin'
& $taskCopy -O binary --only-section=.assets $taskLinked $taskBody
if($LASTEXITCODE -ne 0) {throw 'Asset extraction failed'}
if(-not ('HmiMonitorCrc' -as [type])) {
 Add-Type @'
public static class HmiMonitorCrc {
 public static uint Compute(byte[] b) { uint c=0xffffffff; foreach(byte v in b) {c^=v;for(int k=0;k<8;k++)c=(c>>1)^((c&1)!=0?0xedb88320u:0u);} return ~c; }
}
'@
}
$taskData=[IO.File]::ReadAllBytes($taskBody);$taskCrc=[HmiMonitorCrc]::Compute($taskData)
$taskExpected=Join-Path $taskOut 'expected-crc.bin';[IO.File]::WriteAllBytes($taskExpected,[BitConverter]::GetBytes($taskCrc))
$taskHeader=Join-Path $taskOut 'asset-header.bin'
[IO.File]::WriteAllBytes($taskHeader,([byte[]]([BitConverter]::GetBytes([uint32]0x41535434)+[BitConverter]::GetBytes([uint32]$taskData.Length)+[BitConverter]::GetBytes($taskCrc)+[BitConverter]::GetBytes([uint32]1))))
$taskFinal=Join-Path $taskOut 'HmiMonitor.elf'
& $taskCopy --update-section ('.asset_expectation='+$taskExpected) --update-section ('.assets_header='+$taskHeader) $taskLinked $taskFinal
if($LASTEXITCODE -ne 0) {throw 'Manifest insertion failed'}
if($FinalApplication) {
 # Resolve the actual linked offsets; consumers never guess compiler array order.
 $taskSymbols=@{}
 foreach($taskLine in (& (Join-Path $Tools 'arm-none-eabi-nm.exe') -n $taskFinal)) {
  if($taskLine -match '^([0-9a-fA-F]+)\s+\w\s+(\S+)$'){$taskSymbols[$Matches[2]]=[Convert]::ToInt64($Matches[1],16)}
 }
 if($LASTEXITCODE -ne 0){throw 'Asset address map failed'}
 $taskReportPath=Join-Path $taskOut 'pages-report.json'
 $taskAssetReport=Get-Content -LiteralPath $taskReportPath -Raw|ConvertFrom-Json
 foreach($taskAsset in $taskAssetReport) {
  if(!$taskSymbols.ContainsKey($taskAsset.symbol)){throw ('Asset symbol missing: '+$taskAsset.symbol)}
  $taskAddress=$taskSymbols[$taskAsset.symbol];$taskOffset=$taskAddress-0x90000000L
  if($taskOffset -lt 256 -or $taskOffset+$taskAsset.bytes -gt $taskData.Length+256 -or $taskOffset%32){throw 'Invalid or unaligned asset offset'}
  $taskAsset|Add-Member -NotePropertyName address -NotePropertyValue ('0x{0:X8}' -f $taskAddress) -Force
  $taskAsset|Add-Member -NotePropertyName flash_offset -NotePropertyValue ('0x{0:X6}' -f $taskOffset) -Force
 }
 [IO.File]::WriteAllText($taskReportPath,($taskAssetReport|ConvertTo-Json -Depth 8))
}
foreach($taskFormat in @('binary','ihex')) {
 $taskExt=if($taskFormat -eq 'binary'){'bin'}else{'hex'}
 & $taskCopy -O $taskFormat --remove-section=.assets --remove-section=.assets_header $taskFinal (Join-Path $taskOut ('Internal_CPU.'+$taskExt))
 if($LASTEXITCODE -ne 0) {throw 'Internal export failed'}
 & $taskCopy -O $taskFormat --only-section=.assets --only-section=.assets_header $taskFinal (Join-Path $taskOut ('External_Assets.'+$taskExt))
 if($LASTEXITCODE -ne 0) {throw 'External export failed'}
}
& (Join-Path $Tools 'arm-none-eabi-size.exe') $taskFinal
if($LASTEXITCODE -ne 0) {throw 'Size check failed'}
Get-Item (Join-Path $taskOut 'Internal_CPU.bin'),(Join-Path $taskOut 'External_Assets.bin')|Select-Object Name,Length
Write-Output $(if($FinalApplication){'Built unified application: Home / Settings / Service / DIAG + acquisition + burner supervisor + RS485 master. No board programming.'}else{'Built standalone monitor profile. No board programming.'})

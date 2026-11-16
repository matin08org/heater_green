param(
 [string]$HomePng,[string]$UiConfig,[string]$BoardConfig,
 [string]$Tools='C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin',
 [string]$HostCompiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe',
 [switch]$RequireProductionReady,[switch]$SkipTests,[switch]$SkipSourceArchive
)
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot;$taskOut=Join-Path $taskRoot 'Debug/FinalV1';$taskRelease=Join-Path $taskRoot 'Release/FinalV1'
New-Item -ItemType Directory -Force $taskRelease|Out-Null
$taskMarker=Join-Path $taskRelease '.building';[IO.File]::WriteAllText($taskMarker,'Build not complete; do not program stale files.')
$taskTranscript=Join-Path $taskRelease 'build.log'
Start-Transcript -Path $taskTranscript -Force|Out-Null
try {
 & (Join-Path $taskRoot 'Build-HmiMonitor.ps1') -Tools $Tools -FinalApplication -HomePng $HomePng -UiConfig $UiConfig -BoardConfig $BoardConfig
 $taskReadiness=Get-Content -LiteralPath (Join-Path $taskOut 'readiness.json') -Raw|ConvertFrom-Json
 if(!$SkipTests) {
  & (Join-Path $taskRoot 'Tests/hmi/evidence-profile.ps1')
  & (Join-Path $taskRoot 'Tests/flash/test.ps1')
  & (Join-Path $taskRoot 'Tests/hmi/run.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tests/hmi/final.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tools/Hmi/Preview-Rgb565.ps1') -InputFile (Join-Path $taskOut 'home-preview.rgb565') -OutputPng (Join-Path $taskRelease 'home-preview.png')
  & (Join-Path $taskRoot 'Tools/Hmi/Preview-Rgb565.ps1') -InputFile (Join-Path $taskOut 'home-preview.rgb565.running') -OutputPng (Join-Path $taskRelease 'home-running-preview.png')
  & (Join-Path $taskRoot 'Tools/Hmi/Preview-Rgb565.ps1') -InputFile (Join-Path $taskOut 'home-preview.rgb565.palette') -OutputPng (Join-Path $taskRelease 'home-palette-preview.png')
  & (Join-Path $taskRoot 'Tests/burner/run.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tests/ui/run.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tests/max6675/run.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tests/radio/run.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tests/stepper/run.ps1')
  & (Join-Path $taskRoot 'Tests/buzzer/run.ps1')
  & (Join-Path $taskRoot 'Tests/tim1/run.ps1')
  & (Join-Path $taskRoot 'Tests/adc/run.ps1')
  & (Join-Path $taskRoot 'Tests/actuation/run.ps1')
  & (Join-Path $taskRoot 'Tests/actuation/output-run.ps1')
  & (Join-Path $taskRoot 'Tests/scenario/run.ps1') -Compiler $HostCompiler
  & (Join-Path $taskRoot 'Tests/scenario/board-run.ps1') -Compiler $HostCompiler
 }
 & (Join-Path $taskRoot 'Tests/hmi/verify-profile.ps1') -Compiler $HostCompiler -Tools $Tools -ProfileDirectory $taskOut -FinalApplication
 if($RequireProductionReady -and !$taskReadiness.production_ready) {throw 'Production release refused: readiness.json lists unverified hardware/safety items.'}
 $taskName='Main_Board_Cube_finalv1'
 $taskCopies=@{'Internal_CPU.bin'=($taskName+'.bin');'Internal_CPU.hex'=($taskName+'_internal.hex');'External_Assets.bin'=($taskName+'_assets.bin');'External_Assets.hex'=($taskName+'_assets.hex');'HmiMonitor.elf'=($taskName+'.elf');'HmiMonitor.map'=($taskName+'.map');'readiness.json'='readiness.json';'pages-report.json'='pages-report.json'}
 foreach($taskFile in $taskCopies.Keys){Copy-Item -LiteralPath (Join-Path $taskOut $taskFile) -Destination (Join-Path $taskRelease $taskCopies[$taskFile]) -Force}
 & (Join-Path $Tools 'arm-none-eabi-objcopy.exe') -O ihex (Join-Path $taskOut 'HmiMonitor.elf') (Join-Path $taskRelease ($taskName+'.hex'))
 if($LASTEXITCODE -ne 0){throw 'Combined HEX export failed'}
 $taskFiles=@()
 foreach($taskFile in @(($taskName+'.bin'),($taskName+'_assets.bin'),($taskName+'.hex'))) {
  $taskPath=Join-Path $taskRelease $taskFile
  $taskFiles+=@([ordered]@{name=$taskFile;bytes=(Get-Item -LiteralPath $taskPath).Length;sha256=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash})
 }
 $taskManifest=[ordered]@{name=$taskName;schema=1;display='ST7796S landscape 480x320';spi_hz=16000000;cpu_address='0x08000000';assets_address='0x90000000';production_ready=$taskReadiness.production_ready;build_class=$taskReadiness.build_class;operation_mode=$taskReadiness.operation_mode;tests_passed=(!$SkipTests);files=$taskFiles}
 [IO.File]::WriteAllText((Join-Path $taskRelease 'manifest.json'),($taskManifest|ConvertTo-Json -Depth 8))
 Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination (Join-Path $taskRelease 'README.md') -Force
 Copy-Item -LiteralPath (Join-Path $taskRoot 'Docs') -Destination $taskRelease -Recurse -Force
 Copy-Item -LiteralPath (Join-Path $taskOut 'readiness-detail.json') -Destination (Join-Path $taskRelease 'readiness-detail.json') -Force
 & (Join-Path $taskRoot 'Tools/Hmi/Export-HomePanels.ps1') -ProfileDirectory $taskOut -OutputDirectory (Join-Path $taskRelease 'HomePanels')
 if(!$SkipTests) {
  $taskScenario=Join-Path $taskRelease 'Scenario'
  [IO.Directory]::CreateDirectory($taskScenario)|Out-Null
  Get-ChildItem -LiteralPath (Join-Path $taskRoot 'Debug/Scenario') -File | Where-Object {$_.Extension -in @('.json','.csv','.txt','.exe')} | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $taskScenario -Force}
 }
 if(!$SkipSourceArchive) {
 & (Join-Path $taskRoot 'Tools/Export-Source.ps1') -Destination (Join-Path $taskRelease ($taskName+'_source.zip'))
 }
 Get-ChildItem -LiteralPath (Join-Path $taskRoot 'Tests') -File -Recurse -Filter '*.exe' |
  ForEach-Object {Remove-Item -LiteralPath $_.FullName -Force}
 Remove-Item -LiteralPath $taskMarker -Force
 Write-Host 'FINAL V1 BUILD COMPLETE. Integrated application; no board programming performed.'
 Write-Host ('Production ready: '+$taskReadiness.production_ready+'; outputs remain inhibited pending commissioning.')
 Write-Host (Join-Path $taskRelease ($taskName+'.bin'))
} finally {Stop-Transcript|Out-Null}

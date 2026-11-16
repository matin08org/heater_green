param(
 [string]$Serial,[int]$FrequencyKHz=1000,[ValidateSet('1MHz','250kHz')][string]$FlashClock='1MHz',
 [ValidateRange(1,5)][int]$MaxAttempts=3,[switch]$CheckOnly,
 [ValidateSet('UR','NORMAL','HOTPLUG')][string]$ConnectionMode='UR'
)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false
$taskRoot=$PSScriptRoot;$taskDir=Join-Path $taskRoot 'Release/FinalV1'
if(Test-Path -LiteralPath (Join-Path $taskDir '.building')){throw 'Build incomplete. Run Build-Final.ps1 successfully first.'}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskDir 'manifest.json') -Raw|ConvertFrom-Json
if($taskManifest.schema -ne 1 -or $taskManifest.cpu_address -ne '0x08000000' -or $taskManifest.assets_address -ne '0x90000000'){throw 'Wrong firmware manifest.'}
foreach($f in $taskManifest.files) {
 if([IO.Path]::GetFileName($f.name) -ne $f.name){throw 'Unsafe manifest file path.'}
 $p=Join-Path $taskDir $f.name
 if((Get-Item -LiteralPath $p).Length -ne $f.bytes -or (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash -ne $f.sha256){throw ('Firmware package hash mismatch: '+$f.name)}
}
$taskCpuPath=Join-Path $taskDir ($taskManifest.name+'.bin');$taskAssetsPath=Join-Path $taskDir ($taskManifest.name+'_assets.bin')
$taskCpu=[IO.File]::ReadAllBytes($taskCpuPath);$taskAssets=[IO.File]::ReadAllBytes($taskAssetsPath)
if($taskCpu.Length -gt 524288 -or [BitConverter]::ToUInt32($taskCpu,0) -ne 0x20040000 -or [BitConverter]::ToUInt32($taskCpu,4) -lt 0x08000001 -or [BitConverter]::ToUInt32($taskCpu,4) -ge 0x08080000){throw 'Invalid CPU binary/vectors.'}
Write-Host ('Package verified. CPU '+$taskCpu.Length+' bytes -> 0x08000000; assets '+$taskAssets.Length+' bytes -> 0x90000000.')
Write-Host ('Build class: '+$taskManifest.build_class+'; production_ready='+$taskManifest.production_ready)
if($taskManifest.operation_mode){Write-Host ('Operation mode: '+$taskManifest.operation_mode)}
if($taskManifest.operation_mode -eq 'BENCH_OUTPUT_TEST_DISARMED') {
 Write-Host 'OUTPUT TEST: Service arming can energize Q7/Q1. Isolate fuel; disconnect valve coil and ignition transformer; fit suitable dummy loads before arming.'
}
if($CheckOnly){Write-Host 'CHECK ONLY: no connection or programming.';return}
$taskCli=Get-ChildItem 'C:/ST/STM32CubeIDE_2.1.1' -Recurse -Filter STM32_Programmer_CLI.exe|Select-Object -First 1 -ExpandProperty FullName
if(!$taskCli){throw 'STM32CubeProgrammer CLI not found.'}
$taskLoader=Join-Path $taskRoot "STM32F412_W25Q16_V1.0_QSPI_$FlashClock.stldr"
if(!(Test-Path -LiteralPath $taskLoader)){throw 'Matching external flash loader is missing.'}
. (Join-Path $taskRoot 'Flash-Verify.ps1')
$taskResetMode=if($ConnectionMode -eq 'UR'){'HWrst'}else{'SWrst'}
$taskConnection=@('-c','port=SWD',"freq=$FrequencyKHz",("mode="+$ConnectionMode),("reset="+$taskResetMode))
if($Serial){$taskConnection+=@("sn=$Serial")}
$taskConnection+=@('-el',$taskLoader)
$taskWork=Join-Path $taskRoot 'Debug/FinalV1/program';New-Item -ItemType Directory -Force $taskWork|Out-Null
$taskBackup=Join-Path $taskWork ('internal-before-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.bin')
& $taskCli @taskConnection -u 0x08000000 524288 $taskBackup
if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $taskBackup) -or (Get-Item -LiteralPath $taskBackup).Length -ne 524288){throw 'Internal bootloader backup failed; programming not started.'}
Write-Host 'Internal firmware will replace the previous bootloader. Backup saved.'
$taskReadback=Join-Path $taskWork 'readback.bin'
$taskRead={param([long]$Address,[int]$Count)
 [IO.File]::WriteAllBytes($taskReadback,[byte[]]@())
 & $taskCli @taskConnection -u ('0x{0:X8}'-f $Address) "$Count" $taskReadback|ForEach-Object {Write-Host $_}
 $ok=$LASTEXITCODE -eq 0
 return [pscustomobject]@{Success=$ok;Data=if($ok){[IO.File]::ReadAllBytes($taskReadback)}else{[byte[]]@()}}
}
$taskWrite={param([long]$Address,[byte[]]$Data)
 $p=Join-Path $taskWork ('{0:X8}.bin'-f $Address);[IO.File]::WriteAllBytes($p,$Data)
 & $taskCli @taskConnection -d $p ('0x{0:X8}'-f $Address) -v|ForEach-Object {Write-Host $_}
 return [pscustomobject]@{Success=$LASTEXITCODE -eq 0}
}
$taskNoReset={return [pscustomobject]@{Success=$true}}
$null=Invoke-VerifiedFlashImage -Image $taskAssets -Read $taskRead -Write $taskWrite -Reset $taskNoReset -MaxAttempts $MaxAttempts
& $taskCli @taskConnection -d $taskCpuPath 0x08000000 -v
if($LASTEXITCODE -ne 0){throw 'Internal programming/verification failed. No reset requested.'}
for($i=0;$i -lt 2;$i++) {
 $read=& $taskRead 0x08000000 $taskCpu.Length
 if(!(Test-FlashBytes $taskCpu $read)){throw 'Internal full readback mismatch. No reset requested.'}
 $read=& $taskRead 0x90000000L $taskAssets.Length
 if(!(Test-FlashBytes $taskAssets $read)){throw 'External full readback mismatch. No reset requested.'}
}
& $taskCli @taskConnection -rst
if($LASTEXITCODE -ne 0){throw 'Images verified; board reset failed.'}
Write-Host 'Both matching images verified twice; reset requested.'

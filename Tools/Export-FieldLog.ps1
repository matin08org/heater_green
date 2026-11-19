param(
 [Parameter(Mandatory=$true)][string]$RamDump,
 [string]$Elf=(Join-Path $PSScriptRoot '../Release/FinalV1/Main_Board_Cube_finalv1.elf'),
 [string]$Destination=(Join-Path $PSScriptRoot '../Debug/Field/log.csv'),
 [long]$BaseAddress=0x20000000,
 [string]$Tools='C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin'
)
$ErrorActionPreference='Stop'
$taskSymbols=@{}
foreach($taskLine in (& (Join-Path $Tools 'arm-none-eabi-nm.exe') -S -n $Elf)) {
 if($taskLine -match '^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\w\s+(ring|head|count)$') {
  $taskSymbols[$Matches[3]]=@([Convert]::ToInt64($Matches[1],16),[Convert]::ToInt64($Matches[2],16))
 }
}
if($LASTEXITCODE -ne 0){throw 'ELF symbol lookup failed'}
foreach($taskName in @('ring','head','count')){if(!$taskSymbols.ContainsKey($taskName)){throw ('Missing log symbol: '+$taskName)}}
if($taskSymbols.ring[1] -ne 13824 -or $taskSymbols.head[1] -ne 2 -or $taskSymbols.count[1] -ne 2){throw 'Unsupported log layout; use the matching firmware ELF'}
$taskBytes=[IO.File]::ReadAllBytes([IO.Path]::GetFullPath($RamDump))
foreach($taskName in $taskSymbols.Keys){$taskOffset=$taskSymbols[$taskName][0]-$BaseAddress;if($taskOffset -lt 0 -or $taskOffset+$taskSymbols[$taskName][1] -gt $taskBytes.Length){throw 'RAM dump does not cover log symbols'}}
$taskHead=[BitConverter]::ToUInt16($taskBytes,($taskSymbols.head[0]-$BaseAddress))
$taskCount=[BitConverter]::ToUInt16($taskBytes,($taskSymbols.count[0]-$BaseAddress))
if($taskHead -ge 128 -or $taskCount -gt 128){throw 'Invalid log ring; wrong ELF or inconsistent RAM dump'}
$taskRows=@(for($taskIndex=0;$taskIndex -lt $taskCount;$taskIndex++) {
 $taskOffset=[int]($taskSymbols.ring[0]-$BaseAddress+(($taskHead+128-$taskCount+$taskIndex)%128)*108)
 $taskLevel=[BitConverter]::ToUInt32($taskBytes,$taskOffset+8)
 if($taskLevel -gt 3){throw 'Invalid severity; wrong ELF or inconsistent RAM dump'}
 [pscustomobject]@{sequence=[BitConverter]::ToUInt32($taskBytes,$taskOffset);uptime_ms=[BitConverter]::ToUInt32($taskBytes,$taskOffset+4);severity=@('DEBUG','INFO','WARN','ERROR')[$taskLevel];module=[Text.Encoding]::ASCII.GetString($taskBytes,$taskOffset+12,8).Split([char]0)[0];message=[Text.Encoding]::ASCII.GetString($taskBytes,$taskOffset+20,88).Split([char]0)[0]}
})
$taskTarget=[IO.Path]::GetFullPath($Destination)
[IO.Directory]::CreateDirectory((Split-Path $taskTarget -Parent))|Out-Null
$taskRows|Export-Csv -LiteralPath $taskTarget -NoTypeInformation -Encoding UTF8
Write-Output ('Exported '+$taskRows.Count+' RAM log entries: '+$taskTarget+'; no hardware connection performed.')

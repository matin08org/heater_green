param([string]$Tools='C:/ST/STM32CubeIDE_2.1.1/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskElf=Join-Path $taskRoot 'Debug/FinalV1/HmiMonitor.elf'
$taskOut=Join-Path $taskRoot 'Debug/FieldTest'
[IO.Directory]::CreateDirectory($taskOut)|Out-Null
$taskOffsets=@{}
foreach($taskLine in (& (Join-Path $Tools 'arm-none-eabi-nm.exe') -n $taskElf)) {
 if($taskLine -match '^([0-9a-fA-F]+)\s+\w\s+(ring|head|count)$'){$taskOffsets[$Matches[2]]=[int]([Convert]::ToInt64($Matches[1],16)-0x20000000)}
}
$taskBytes=[byte[]]::new(262144)
[BitConverter]::GetBytes([uint16]1).CopyTo($taskBytes,$taskOffsets.head)
[BitConverter]::GetBytes([uint16]2).CopyTo($taskBytes,$taskOffsets.count)
foreach($taskPair in @(@(127,101),@(0,102))) {
 $taskOffset=$taskOffsets.ring+$taskPair[0]*108
 [BitConverter]::GetBytes([uint32]$taskPair[1]).CopyTo($taskBytes,$taskOffset)
 [BitConverter]::GetBytes([uint32]12345).CopyTo($taskBytes,$taskOffset+4)
 [BitConverter]::GetBytes([uint32]2).CopyTo($taskBytes,$taskOffset+8)
 [Text.Encoding]::ASCII.GetBytes('FIELD').CopyTo($taskBytes,$taskOffset+12)
 [Text.Encoding]::ASCII.GetBytes('fixture,quoted').CopyTo($taskBytes,$taskOffset+20)
}
$taskDump=Join-Path $taskOut 'fixture.bin';$taskCsv=Join-Path $taskOut 'fixture.csv'
[IO.File]::WriteAllBytes($taskDump,$taskBytes)
& (Join-Path $taskRoot 'Tools/Export-FieldLog.ps1') -RamDump $taskDump -Elf $taskElf -Destination $taskCsv -Tools $Tools
$taskRows=@(Import-Csv $taskCsv)
if($taskRows.Count -ne 2 -or $taskRows[0].sequence -ne 101 -or $taskRows[1].sequence -ne 102 -or $taskRows[0].message -ne 'fixture,quoted'){throw 'Ring wrap/order/CSV escaping regression'}
[BitConverter]::GetBytes([uint16]129).CopyTo($taskBytes,$taskOffsets.count)
[IO.File]::WriteAllBytes($taskDump,$taskBytes)
$taskRejected=$false
try {& (Join-Path $taskRoot 'Tools/Export-FieldLog.ps1') -RamDump $taskDump -Elf $taskElf -Destination $taskCsv -Tools $Tools}
catch {if($_.Exception.Message -notmatch 'Invalid log ring'){throw};$taskRejected=$true}
if(!$taskRejected){throw 'Corrupt dump accepted'}
Write-Output 'PASS field CSV: matching ELF layout, wrapped ring order, CSV escaping and corrupt dump rejection; synthetic RAM only.'

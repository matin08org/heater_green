param([string]$Destination=(Join-Path $PSScriptRoot 'Delivery'))
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'Program-Final.ps1') -CheckOnly
$taskNow=[TimeZoneInfo]::ConvertTimeBySystemTimeZoneId([DateTime]::UtcNow,'Iran Standard Time')
$taskName='Main_Board_Cube_finalv1_delivery_'+$taskNow.ToString('yyyyMMdd-HHmmss')
$taskPackage=Join-Path ([IO.Path]::GetFullPath($Destination)) $taskName
$taskRelease=Join-Path $taskPackage 'Release/FinalV1'
[IO.Directory]::CreateDirectory($taskRelease)|Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Release/FinalV1') -File | ForEach-Object {
 Copy-Item -LiteralPath $_.FullName -Destination $taskRelease
}
if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'Release/FinalV1/Scenario')) {
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Release/FinalV1/Scenario') -Destination $taskRelease -Recurse
}
if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'Release/FinalV1/HomePanels')) {
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Release/FinalV1/HomePanels') -Destination $taskRelease -Recurse
}
foreach($taskFile in @('Program-Final.ps1','Flash-Verify.ps1','Debug-Adc.ps1','Run-Scenario.ps1','STM32F412_W25Q16_V1.0_QSPI_1MHz.stldr','STM32F412_W25Q16_V1.0_QSPI_250kHz.stldr')) {
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskFile) -Destination $taskPackage
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Docs') -Destination $taskPackage -Recurse
$taskReadme="Engineering handover: real-input firmware; current profile PHYSICAL_INHIBITED.`nRead README.md and Docs/Status.md. Runtime simulation and test arming are removed.`nFile-only check: .\Program-Final.ps1 -CheckOnly`nProgramming matching CPU/assets: .\Program-Final.ps1`nHost scenario checks: .\Run-Scenario.ps1`nPackaging does not connect to or program hardware."
[IO.File]::WriteAllText((Join-Path $taskPackage 'DELIVERY-README.txt'),$taskReadme)
& (Join-Path $taskPackage 'Program-Final.ps1') -CheckOnly
$taskHashes=Get-ChildItem -LiteralPath $taskPackage -File -Recurse | Sort-Object FullName | ForEach-Object {
 $taskRelative=$_.FullName.Substring($taskPackage.Length+1).Replace('\','/')
 (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash+'  '+$taskRelative
}
[IO.File]::WriteAllLines((Join-Path $taskPackage 'SHA256SUMS.txt'),$taskHashes)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskZip=$taskPackage+'.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($taskPackage,$taskZip,[IO.Compression.CompressionLevel]::Optimal,$true)
$taskArchive=[IO.Compression.ZipFile]::OpenRead($taskZip)
try {
 foreach($taskImage in @('Main_Board_Cube_finalv1.bin','Main_Board_Cube_finalv1_assets.bin','Main_Board_Cube_finalv1_source.zip')) {
  $taskEntry=$taskArchive.Entries | Where-Object {$_.FullName.Replace('\','/') -eq ($taskName+'/Release/FinalV1/'+$taskImage)} | Select-Object -First 1
  if(!$taskEntry){throw ('Archive entry missing: '+$taskImage)}
  $taskStream=$taskEntry.Open();$taskSha=[Security.Cryptography.SHA256]::Create()
  try {$taskHash=[BitConverter]::ToString($taskSha.ComputeHash($taskStream)).Replace('-','')}
  finally {$taskStream.Dispose();$taskSha.Dispose()}
  if($taskHash -ne (Get-FileHash -LiteralPath (Join-Path $taskRelease $taskImage) -Algorithm SHA256).Hash){throw 'Archive content hash mismatch'}
 }
} finally {$taskArchive.Dispose()}
[IO.File]::WriteAllText(($taskZip+'.sha256'),(Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash+'  '+[IO.Path]::GetFileName($taskZip))
Write-Host ('DELIVERY VERIFIED: '+$taskZip)
Write-Host ('Bytes: '+(Get-Item -LiteralPath $taskZip).Length)
Write-Host 'No board connection or programming. Engineering handover; START remains inhibited.'

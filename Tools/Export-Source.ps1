param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskFolders=@('Core','Drivers','Libraries','Tools','Tests','Assets','Docs')
$taskRootFiles=@('README.md','.gitignore','Main_Board_Cube.ioc','Build-Final.ps1','Build-HmiMonitor.ps1','Program-Final.ps1','Package-Delivery.ps1','Flash-Verify.ps1','STM32F412RETX_FLASH.ld','STM32F412_W25Q16_V1.0_QSPI_1MHz.stldr','STM32F412_W25Q16_V1.0_QSPI_250kHz.stldr')
$taskFiles=@(foreach($taskFolder in $taskFolders){Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskFolder) -File -Recurse})
$taskFiles+=@(foreach($taskFile in $taskRootFiles){Get-Item -LiteralPath (Join-Path $taskRoot $taskFile)})
$taskFiles=@($taskFiles|Where-Object {$_.Extension -notin @('.exe','.o','.obj','.d','.su','.log','.zip','.rar','.elf','.map','.stackdump')})
if(@($taskFiles|Where-Object {$_.FullName -match '\\(Backups|Debug|Release|Delivery)\\'}).Count){throw 'Generated/recovery files entered source selection'}
$taskTarget=[IO.Path]::GetFullPath($Destination)
[IO.Directory]::CreateDirectory((Split-Path $taskTarget -Parent))|Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$taskStream=[IO.File]::Open($taskTarget,[IO.FileMode]::Create)
$taskArchive=[IO.Compression.ZipArchive]::new($taskStream,[IO.Compression.ZipArchiveMode]::Create)
try {
 foreach($taskFile in $taskFiles) {
  $taskRelative=$taskFile.FullName.Substring($taskRoot.Length+1).Replace('\','/')
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive,$taskFile.FullName,$taskRelative,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
 }
} finally {$taskArchive.Dispose();$taskStream.Dispose()}
Write-Output ('Source archive: '+$taskTarget+' ('+$taskFiles.Count+' files; generated outputs and device backups excluded)')

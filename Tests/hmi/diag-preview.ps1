param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=Join-Path $taskRoot 'Debug/HmiMonitor'
New-Item -ItemType Directory -Force $taskOut | Out-Null
& (Join-Path $taskRoot 'Tools/Hmi/Export-Font.ps1') -OutputBase (Join-Path $taskOut 'preview-font') -PixelSize 14 -Symbol monitor_font
$taskSources=@('Core/Src/DiagLog.c','Libraries/Hmi/src/HmiTypes.c','Libraries/Hmi/src/HmiText.c','Libraries/Hmi/src/HmiDiag.c')|ForEach-Object {Join-Path $taskRoot $_}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Libraries/Hmi/include') -I (Join-Path $taskRoot 'Core/Inc') @taskSources (Join-Path $taskOut 'preview-font.c') (Join-Path $PSScriptRoot 'diag-preview.c') -o (Join-Path $taskOut 'diag-preview.exe')
if($LASTEXITCODE -ne 0) {throw 'DIAG preview compile failed'}
& (Join-Path $taskOut 'diag-preview.exe') (Join-Path $taskOut 'diag-preview.rgb565')
if($LASTEXITCODE -ne 0) {throw 'DIAG preview render failed'}
Add-Type -AssemblyName System.Drawing
$taskBytes=[IO.File]::ReadAllBytes((Join-Path $taskOut 'diag-preview.rgb565'))
$taskImage=[Drawing.Bitmap]::new(480,320)
try {
 for($taskY=0;$taskY -lt 320;$taskY++) {for($taskX=0;$taskX -lt 480;$taskX++) {
  $taskAt=2*($taskY*480+$taskX);$taskRgb=$taskBytes[$taskAt] -bor ([int]$taskBytes[$taskAt+1] -shl 8)
  $taskR=($taskRgb -shr 11) -band 31;$taskG=($taskRgb -shr 5) -band 63;$taskB=$taskRgb -band 31
  $taskImage.SetPixel($taskX,$taskY,[Drawing.Color]::FromArgb(($taskR -shl 3) -bor ($taskR -shr 2),($taskG -shl 2) -bor ($taskG -shr 4),($taskB -shl 3) -bor ($taskB -shr 2)))
 }}
 $taskImage.Save((Join-Path $taskOut 'diag-preview.png'),[Drawing.Imaging.ImageFormat]::Png)
} finally {$taskImage.Dispose()}
Write-Output (Join-Path $taskOut 'diag-preview.png')

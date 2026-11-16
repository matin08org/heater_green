param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=Join-Path $taskRoot 'Debug/Hmi'
New-Item -ItemType Directory -Force $taskOut | Out-Null
Add-Type -AssemblyName System.Drawing
$taskBmp=[Drawing.Bitmap]::new(120,80)
$taskG=[Drawing.Graphics]::FromImage($taskBmp)
$taskPen=[Drawing.Pen]::new([Drawing.Color]::FromArgb(85,85,85),2)
try {
 $taskG.Clear([Drawing.Color]::Black)
 $taskG.DrawRectangle($taskPen,1,1,117,77)
 $taskG.FillRectangle([Drawing.Brushes]::White,8,65,104,5)
 $taskBmp.Save((Join-Path $taskOut 'card-source.png'),[Drawing.Imaging.ImageFormat]::Png)
} finally {$taskPen.Dispose();$taskG.Dispose();$taskBmp.Dispose()}
& (Join-Path $taskRoot 'Tools/Hmi/Export-Image2.ps1') -InputPng (Join-Path $taskOut 'card-source.png') -OutputBase (Join-Path $taskOut 'card') -Symbol demo_card -Palette 0x0841,0x07E0,0xFFFF,0x07E0
& (Join-Path $taskRoot 'Tools/Hmi/Export-Font.ps1') -OutputBase (Join-Path $taskOut 'font') -PixelSize 14 -Symbol demo_font
$taskSources=@('HmiTypes','HmiImage','HmiText') | ForEach-Object {Join-Path $taskRoot ('Libraries/Hmi/src/'+$_+'.c')}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Libraries/Hmi/include') -I (Join-Path $taskRoot 'Libraries/Hmi/examples') @taskSources (Join-Path $taskRoot 'Libraries/Hmi/examples/HmiCard.c') (Join-Path $taskOut 'card.c') (Join-Path $taskOut 'font.c') (Join-Path $PSScriptRoot 'preview.c') -o (Join-Path $taskOut 'hmi-preview.exe')
if($LASTEXITCODE -ne 0) {throw 'Preview compile failed'}
& (Join-Path $taskOut 'hmi-preview.exe') (Join-Path $taskOut 'card.rgb565')
if($LASTEXITCODE -ne 0) {throw 'Preview rendering failed'}
$taskBytes=[IO.File]::ReadAllBytes((Join-Path $taskOut 'card.rgb565'))
$taskImage=[Drawing.Bitmap]::new(120,80)
try {
 for($taskY=0;$taskY -lt 80;$taskY++) {for($taskX=0;$taskX -lt 120;$taskX++) {
  $taskAt=2*($taskY*120+$taskX)
  $taskRgb=$taskBytes[$taskAt] -bor ([int]$taskBytes[$taskAt+1] -shl 8)
  $taskR=($taskRgb -shr 11) -band 31;$taskG=($taskRgb -shr 5) -band 63;$taskB=$taskRgb -band 31
  $taskImage.SetPixel($taskX,$taskY,[Drawing.Color]::FromArgb(($taskR -shl 3) -bor ($taskR -shr 2),($taskG -shl 2) -bor ($taskG -shr 4),($taskB -shl 3) -bor ($taskB -shr 2)))
 }}
 $taskImage.Save((Join-Path $taskOut 'card-rendered.png'),[Drawing.Imaging.ImageFormat]::Png)
} finally {$taskImage.Dispose()}
Write-Output (Join-Path $taskOut 'card-rendered.png')

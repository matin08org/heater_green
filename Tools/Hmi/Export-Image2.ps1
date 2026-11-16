param(
 [Parameter(Mandatory=$true)][string]$InputPng,
 [Parameter(Mandatory=$true)][string]$OutputBase,
 [string]$Symbol='hmi_image',
 [string[]]$SourceColors=@('000000','555555','AAAAAA','FFFFFF'),
 [uint16[]]$Palette=@(0x0000,0x52AA,0xAD55,0xFFFF),
 [switch]$Quantize
)
$ErrorActionPreference='Stop'
if($SourceColors.Count -ne 4 -or $Palette.Count -ne 4 -or $Symbol -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {throw 'Four source colors, four RGB565 palette entries and a valid C identifier are required.'}
if(($SourceColors | Select-Object -Unique).Count -ne 4) {throw 'Source colors must be distinct.'}
Add-Type -AssemblyName System.Drawing
$taskInput=[IO.Path]::GetFullPath($InputPng)
$taskBase=[IO.Path]::GetFullPath($OutputBase)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskBase)) | Out-Null
$taskColors=@($SourceColors | ForEach-Object {[Drawing.ColorTranslator]::FromHtml('#'+$_.TrimStart('#'))})
$taskImage=[Drawing.Bitmap]::new($taskInput)
$taskPreview=$null
try {
 if($taskImage.Width -gt 65535 -or $taskImage.Height -gt 65535) {throw 'Image dimensions exceed uint16.'}
 $taskStride=[int][Math]::Ceiling($taskImage.Width/4.0)
 $taskBytes=[byte[]]::new($taskStride*$taskImage.Height)
 $taskPreview=[Drawing.Bitmap]::new($taskImage.Width,$taskImage.Height)
 $taskChanged=0
 for($taskY=0;$taskY -lt $taskImage.Height;$taskY++) {
  for($taskX=0;$taskX -lt $taskImage.Width;$taskX++) {
   $taskPixel=$taskImage.GetPixel($taskX,$taskY)
   if($taskPixel.A -ne 255) {throw "Alpha at ($taskX,$taskY); I2 assets must be opaque."}
   $taskBest=0; $taskDistance=[int]::MaxValue
   for($taskK=0;$taskK -lt 4;$taskK++) {
    $taskC=$taskColors[$taskK]
    $taskD=[int]([Math]::Pow($taskPixel.R-$taskC.R,2)+[Math]::Pow($taskPixel.G-$taskC.G,2)+[Math]::Pow($taskPixel.B-$taskC.B,2))
    if($taskD -lt $taskDistance) {$taskDistance=$taskD;$taskBest=$taskK}
   }
   if($taskDistance -ne 0) {
    if(!$Quantize) {throw "Extra color at ($taskX,$taskY). Use -Quantize after reviewing the design."}
    $taskChanged++
   }
   $taskAt=$taskY*$taskStride+[int][Math]::Floor($taskX/4.0)
   $taskBytes[$taskAt]=$taskBytes[$taskAt] -bor ($taskBest -shl (6-2*($taskX%4)))
   $taskRgb=[int]$Palette[$taskBest]
   $taskR=($taskRgb -shr 11) -band 31; $taskG=($taskRgb -shr 5) -band 63; $taskB=$taskRgb -band 31
   $taskPreview.SetPixel($taskX,$taskY,[Drawing.Color]::FromArgb(($taskR -shl 3) -bor ($taskR -shr 2),($taskG -shl 2) -bor ($taskG -shr 4),($taskB -shl 3) -bor ($taskB -shr 2)))
  }
 }
 [IO.File]::WriteAllBytes($taskBase+'.i2',$taskBytes)
 $taskMeta=[ordered]@{format='HMI_I2_MSB_ROWS_V1';width=$taskImage.Width;height=$taskImage.Height;stride=$taskStride;size=$taskBytes.Length;source_colors=$SourceColors;palette_rgb565=@($Palette|ForEach-Object {'{0:X4}' -f $_});quantized_pixels=$taskChanged}
 [IO.File]::WriteAllText($taskBase+'.json',($taskMeta|ConvertTo-Json -Depth 5))
 $taskLines=[Collections.Generic.List[string]]::new()
 $taskLines.Add('#include "HmiImage.h"')
 $taskLines.Add('#ifndef HMI_ASSET_STORAGE')
 $taskLines.Add('#define HMI_ASSET_STORAGE')
 $taskLines.Add('#endif')
 $taskLines.Add(('static const uint8_t {0}_data[] HMI_ASSET_STORAGE = {{' -f $Symbol))
 for($taskAt=0;$taskAt -lt $taskBytes.Length;$taskAt+=16) {
  $taskEnd=[Math]::Min($taskAt+15,$taskBytes.Length-1)
  $taskLines.Add('    '+(($taskBytes[$taskAt..$taskEnd]|ForEach-Object {'0x{0:X2}' -f $_}) -join ',')+',')
 }
 $taskLines.Add('};')
 $taskLines.Add(('const HmiImage2 {0} = {{{0}_data, sizeof({0}_data), {1}, {2}, {3}}};' -f $Symbol,$taskImage.Width,$taskImage.Height,$taskStride))
 [IO.File]::WriteAllLines($taskBase+'.c',$taskLines)
 $taskPreview.Save($taskBase+'.preview.png',[Drawing.Imaging.ImageFormat]::Png)
 Write-Output ("Exported {0}x{1}, {2} bytes, {3} quantized pixels. Palette index order preserved." -f $taskImage.Width,$taskImage.Height,$taskBytes.Length,$taskChanged)
} finally {
 if($taskPreview) {$taskPreview.Dispose()}
 $taskImage.Dispose()
}

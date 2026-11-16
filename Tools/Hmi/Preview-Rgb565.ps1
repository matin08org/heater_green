param([Parameter(Mandatory=$true)][string]$InputFile,[Parameter(Mandatory=$true)][string]$OutputPng,
 [ValidateRange(1,480)][int]$Width=480,[ValidateRange(1,320)][int]$Height=320)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$taskData=[IO.File]::ReadAllBytes([IO.Path]::GetFullPath($InputFile))
if($taskData.Length -ne $Width*$Height*2){throw 'Raw RGB565 size does not match dimensions.'}
$taskImage=[Drawing.Bitmap]::new($Width,$Height)
try {
 for($y=0;$y -lt $Height;$y++){for($x=0;$x -lt $Width;$x++){
  $at=2*($y*$Width+$x);$rgb=$taskData[$at]-bor ([int]$taskData[$at+1]-shl 8)
  $r=($rgb-shr 11)-band 31;$g=($rgb-shr 5)-band 63;$b=$rgb-band 31
  $taskImage.SetPixel($x,$y,[Drawing.Color]::FromArgb(($r-shl 3)-bor ($r-shr 2),($g-shl 2)-bor ($g-shr 4),($b-shl 3)-bor ($b-shr 2)))
 }}
 $taskImage.Save([IO.Path]::GetFullPath($OutputPng),[Drawing.Imaging.ImageFormat]::Png)
} finally {$taskImage.Dispose()}

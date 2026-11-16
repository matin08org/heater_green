param(
 [Parameter(Mandatory=$true)][string]$OutputBase,
 [string]$FontFamily='Consolas',
 [ValidateRange(6,80)][int]$PixelSize=18,
 [string]$Symbol='hmi_font',
 [ValidateSet('Internal','External')][string]$Storage='Internal',
 [ValidateSet(1,2)][int]$CoverageBits=1,
 [ValidateRange(32,126)][int]$FirstChar=32,
 [ValidateRange(32,126)][int]$LastChar=126,
 [switch]$Proportional,[switch]$Bold,[switch]$TrimVertical
)
$ErrorActionPreference='Stop'
if($Symbol -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {throw 'Invalid C identifier.'}
if($LastChar -lt $FirstChar){throw 'Invalid character range.'}
Add-Type -AssemblyName System.Drawing
$taskBase=[IO.Path]::GetFullPath($OutputBase)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskBase)) | Out-Null
$taskStyle=if($Bold){[Drawing.FontStyle]::Bold}else{[Drawing.FontStyle]::Regular}
$taskFont=[Drawing.Font]::new($FontFamily,$PixelSize,$taskStyle,[Drawing.GraphicsUnit]::Pixel)
$taskMeasure=[Drawing.Bitmap]::new(256,256)
$taskGraphics=[Drawing.Graphics]::FromImage($taskMeasure)
$taskFormat=[Drawing.StringFormat]::GenericTypographic.Clone()
$taskFormat.FormatFlags=$taskFormat.FormatFlags -bor [Drawing.StringFormatFlags]::MeasureTrailingSpaces -bor [Drawing.StringFormatFlags]::NoWrap
$taskGlyph=$null; $taskDraw=$null
try {
 if($taskFont.FontFamily.Name -ne $FontFamily) {throw ('Requested font unavailable; substituted '+$taskFont.FontFamily.Name)}
 $taskHint=if($CoverageBits -eq 2){[Drawing.Text.TextRenderingHint]::AntiAliasGridFit}else{[Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit}
 $taskGraphics.TextRenderingHint=$taskHint
 $taskWidth=0
 $taskAdvances=[byte[]]::new($LastChar-$FirstChar+1)
 for($taskC=$FirstChar;$taskC -le $LastChar;$taskC++) {
  $taskSize=$taskGraphics.MeasureString([string][char]$taskC,$taskFont,256,$taskFormat)
  $taskWidth=[Math]::Max($taskWidth,[int][Math]::Ceiling($taskSize.Width)+2)
  $taskAdvances[$taskC-$FirstChar]=[Math]::Max(1,[int][Math]::Ceiling($taskSize.Width)+1)
 }
 $taskHeight=[int][Math]::Ceiling($taskFont.GetHeight($taskGraphics))+2
 if($taskWidth -gt 255 -or $taskHeight -gt 253) {throw 'Font cell exceeds metadata limits.'}
 $taskGlyph=[Drawing.Bitmap]::new($taskWidth,$taskHeight)
 $taskDraw=[Drawing.Graphics]::FromImage($taskGlyph)
 $taskDraw.TextRenderingHint=$taskHint
 $taskInkTop=0
 if($TrimVertical) {
  $taskMin=$taskHeight;$taskMax=-1
  for($taskC=$FirstChar;$taskC -le $LastChar;$taskC++) {
   $taskDraw.Clear([Drawing.Color]::Black)
   $taskDraw.DrawString([string][char]$taskC,$taskFont,[Drawing.Brushes]::White,[Drawing.PointF]::new(1,0),$taskFormat)
   for($taskY=0;$taskY -lt $taskHeight;$taskY++){for($taskX=0;$taskX -lt $taskWidth;$taskX++){
    if($taskGlyph.GetPixel($taskX,$taskY).R -gt 0){$taskMin=[Math]::Min($taskMin,$taskY);$taskMax=[Math]::Max($taskMax,$taskY)}
   }}
  }
  if($taskMax -ge $taskMin){$taskInkTop=$taskMin;$taskHeight=$taskMax-$taskMin+1}
 }
 $taskStride=[int][Math]::Ceiling($taskWidth*$CoverageBits/8.0)
 $taskPrefix=if($Proportional){$taskAdvances.Length}else{0}
 $taskBytes=[byte[]]::new($taskPrefix+($LastChar-$FirstChar+1)*$taskStride*$taskHeight)
 if($Proportional){[Array]::Copy($taskAdvances,0,$taskBytes,0,$taskPrefix)}
 for($taskC=$FirstChar;$taskC -le $LastChar;$taskC++) {
  $taskDraw.Clear([Drawing.Color]::Black)
  $taskDraw.DrawString([string][char]$taskC,$taskFont,[Drawing.Brushes]::White,[Drawing.PointF]::new(1,0),$taskFormat)
  for($taskY=0;$taskY -lt $taskHeight;$taskY++) {
   for($taskX=0;$taskX -lt $taskWidth;$taskX++) {
    $taskPixel=$taskGlyph.GetPixel($taskX,$taskY+$taskInkTop).R
    if($CoverageBits -eq 2) {
     $taskLevel=[int][Math]::Floor(($taskPixel*3+127)/255.0)
     $taskAt=$taskPrefix+(($taskC-$FirstChar)*$taskHeight+$taskY)*$taskStride+[int][Math]::Floor($taskX/4.0)
     $taskBytes[$taskAt]=$taskBytes[$taskAt] -bor ($taskLevel -shl (6-2*($taskX%4)))
    } elseif($taskPixel -ge 128) {
     $taskAt=$taskPrefix+(($taskC-$FirstChar)*$taskHeight+$taskY)*$taskStride+[int][Math]::Floor($taskX/8.0)
     $taskBytes[$taskAt]=$taskBytes[$taskAt] -bor (0x80 -shr ($taskX%8))
    }
   }
  }
 }
 [IO.File]::WriteAllBytes(($taskBase+'.font'+$CoverageBits),$taskBytes)
 $taskMeta=[ordered]@{format=('HMI_FONT'+$CoverageBits+'_MSB_ROWS_V1');coverage_bpp=$CoverageBits;proportional=[bool]$Proportional;family=$FontFamily;pixel_size=$PixelSize;first=$FirstChar;last=$LastChar;width=$taskWidth;height=$taskHeight;advance=$taskWidth;line_height=$taskHeight;size=$taskBytes.Length;storage=$Storage}
 [IO.File]::WriteAllText($taskBase+'.json',($taskMeta|ConvertTo-Json))
 $taskLines=[Collections.Generic.List[string]]::new()
 $taskLines.Add('#include "HmiText.h"')
 $taskLines.Add('#ifndef HMI_ASSET_STORAGE')
 if($Storage -eq 'External') {
  $taskLines.Add('#define HMI_ASSET_STORAGE __attribute__((section(".hmi_assets")))')
 } else {
  $taskLines.Add('#define HMI_ASSET_STORAGE')
 }
 $taskLines.Add('#endif')
 $taskLines.Add(('static const uint8_t {0}_data[] HMI_ASSET_STORAGE = {{' -f $Symbol))
 for($taskAt=0;$taskAt -lt $taskBytes.Length;$taskAt+=16) {
  $taskEnd=[Math]::Min($taskAt+15,$taskBytes.Length-1)
  $taskLines.Add('    '+(($taskBytes[$taskAt..$taskEnd]|ForEach-Object {'0x{0:X2}' -f $_}) -join ',')+',')
 }
 $taskLines.Add('};')
 $taskLines.Add(('const HmiFont {0} = {{{0}_data, sizeof({0}_data), {3}, {4}, {1}, {2}, {1}, {2}, {5}, {6}}};' -f $Symbol,$taskWidth,$taskHeight,$FirstChar,$LastChar,$CoverageBits,[int][bool]$Proportional))
 [IO.File]::WriteAllLines($taskBase+'.c',$taskLines)
 Write-Output ("Exported ASCII {3}..{4}, cell {0}x{1}, {2} bytes, {5}bpp coverage. Check font redistribution rights before shipping." -f $taskWidth,$taskHeight,$taskBytes.Length,$FirstChar,$LastChar,$CoverageBits)
} finally {
 if($taskDraw) {$taskDraw.Dispose()}; if($taskGlyph) {$taskGlyph.Dispose()}
 $taskFormat.Dispose();$taskGraphics.Dispose();$taskMeasure.Dispose();$taskFont.Dispose()
}

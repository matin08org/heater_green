param([Parameter(Mandatory=$true)][string]$Config,[Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$HomePng)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$taskConfig=[IO.Path]::GetFullPath($Config);$taskRoot=Split-Path $taskConfig -Parent
$taskOut=[IO.Path]::GetFullPath($OutputDirectory);[IO.Directory]::CreateDirectory($taskOut)|Out-Null
$taskUi=Get-Content -LiteralPath $taskConfig -Raw|ConvertFrom-Json
if($taskUi.schema -ne 1 -or $taskUi.display.width -ne 480 -or $taskUi.display.height -ne 320 -or $taskUi.display.orientation -ne 'landscape') {throw 'UI schema 1 requires landscape 480x320.'}
if(!$taskUi.pages -or $taskUi.pages.Count -gt 32) {throw '1..32 pages are required.'}
function CQuote([string]$s) {
 if($s -match '[^\x20-\x7e]' -or $s.Length -gt 47) {throw 'Labels/IDs must be printable ASCII, at most 47 characters.'}
 return '"'+$s.Replace('\','\\').Replace('"','\"')+'"'
}
function Color([string]$s,[string]$fallback='FFFF') {
 if(!$s) {$s=$fallback};$s=$s.TrimStart('#')
 if($s -notmatch '^[0-9a-fA-F]{4}$') {throw ('RGB565 color must have four hex digits: '+$s)}
 return '0x'+$s+'U'
}
function Rect($w) {
 if($null -eq $w.x -or $null -eq $w.y -or $w.width -le 0 -or $w.height -le 0 -or $w.x -lt 0 -or $w.y -lt 0 -or $w.x+$w.width -gt 480 -or $w.y+$w.height -gt 320) {throw ('Invalid widget bounds: '+$w.id)}
 foreach($n in @($w.x,$w.y,$w.width,$w.height)) {if([int]$n -ne $n) {throw 'Widget coordinates must be integers.'}}
 return ('{{{0},{1},{2},{3}}}' -f $w.x,$w.y,$w.width,$w.height)
}
$taskIds=@{};foreach($p in $taskUi.pages) {if(!$p.id -or $taskIds.ContainsKey($p.id)) {throw 'Page IDs must be unique and nonempty.'};$taskIds[$p.id]=$true;[void](CQuote $p.id)}
if(!$taskIds.ContainsKey('home')) {throw 'A page named home is required.'}
$taskLines=[Collections.Generic.List[string]]::new()
$taskLines.Add('#include "HmiPages.h"')
$taskLines.Add('extern const HmiFont home_small_font, home_value_font;')
$taskLines.Add('#define ASSET __attribute__((section(".hmi_assets")))')
$taskPageDefs=[Collections.Generic.List[string]]::new();$taskReport=@();$taskPageIndex=0
foreach($p in $taskUi.pages) {
 $taskOriginX=0;$taskOriginY=0
 if($p.coordinate_space -and $p.coordinate_space -notin @('screen','image')){throw 'coordinate_space must be screen or image.'}
 $taskWidgetDefs=[Collections.Generic.List[string]]::new();$taskWidgetIds=@{}
 $taskImages=@();if($p.image) {$taskImages+=@([pscustomobject]@{id='background';image=$p.image;format=$p.format;placement=$p.placement;palette=$p.palette;source_colors=$p.source_colors})}
 foreach($w in $p.widgets) {if($w.image) {$taskImages+=@($w)}}
 foreach($im in $taskImages) {
  $taskInput=if($p.id -eq 'home' -and $im.id -eq 'background' -and $HomePng){[IO.Path]::GetFullPath($HomePng)}else{[IO.Path]::GetFullPath((Join-Path $taskRoot $im.image))}
  if(!(Test-Path -LiteralPath $taskInput)) {throw ('Missing PNG: '+$taskInput)}
  $taskBmp=[Drawing.Bitmap]::new($taskInput)
  try {
   if($taskBmp.Width -gt 480 -or $taskBmp.Height -gt 320) {throw 'PNG must fit landscape 480x320; export at 1x.'}
   $taskColors=[Collections.Generic.List[int]]::new()
   for($y=0;$y -lt $taskBmp.Height;$y++) {for($x=0;$x -lt $taskBmp.Width;$x++) {
    $c=$taskBmp.GetPixel($x,$y);if($c.A -ne 255) {throw ('PNG alpha not supported for opaque assets: '+$taskInput)}
    $rgb=$c.ToArgb();if($taskColors.Count -le 4 -and !$taskColors.Contains($rgb)) {$taskColors.Add($rgb)}
   }}
   $taskMode=if($im.format){[string]$im.format}else{'auto'}
   if($taskMode -eq 'auto') {$taskMode=if($taskColors.Count -le 4){'2bpp'}else{'rgb565'}}
   if($taskMode -notin @('2bpp','i2blend4','rgb565')) {throw 'format must be auto, 2bpp, i2blend4 or rgb565.'}
   if($taskMode -eq '2bpp' -and $taskColors.Count -gt 4 -and !$im.source_colors) {throw '2bpp PNG has >4 colors; use rgb565/auto or specify four source_colors for explicit quantization.'}
   $taskSymbol='asset_'+$taskPageIndex+'_'+$taskWidgetDefs.Count
   $taskX=if($null -ne $im.x){[int]$im.x}else{[int][Math]::Floor((480-$taskBmp.Width)/2.0)}
   $taskY=if($null -ne $im.y){[int]$im.y}else{[int][Math]::Floor((320-$taskBmp.Height)/2.0)}
   if($im.id -eq 'background'){$taskOriginX=$taskX;$taskOriginY=$taskY}
   elseif($p.coordinate_space -eq 'image'){$taskX+=$taskOriginX;$taskY+=$taskOriginY}
   if($im.placement -and $im.placement -ne 'center') {throw 'Only native-size center placement is supported; x/y position widgets explicitly.'}
   $taskBounds=Rect ([pscustomobject]@{id=$im.id;x=$taskX;y=$taskY;width=$taskBmp.Width;height=$taskBmp.Height})
   $taskPalette=@();$taskBytes=$null;$taskStride=0
   if($taskMode -eq 'i2blend4') {
    if($im.palette.Count -ne 4){throw 'Antialiased role images require exactly four RGB565 colors.'}
    $taskPalette=@($im.palette|ForEach-Object {[Convert]::ToUInt16($_.TrimStart('#'),16)})
    $taskStride=$taskBmp.Width;$taskBytes=[byte[]]::new($taskStride*$taskBmp.Height)
    for($y=0;$y -lt $taskBmp.Height;$y++){for($x=0;$x -lt $taskBmp.Width;$x++){
     $c=$taskBmp.GetPixel($x,$y)
     if($c.R%85 -or $c.G%85 -or $c.B%17){throw 'Invalid I2 blend mask: R/G encode roles, B encodes 4-bit coverage.'}
     $taskBytes[$y*$taskStride+$x]=(([int]($c.R/85)) -shl 6) -bor (([int]($c.G/85)) -shl 4) -bor ([int]($c.B/17))
    }}
   } elseif($taskMode -eq '2bpp') {
    $taskSource=@()
    if($im.source_colors) {$taskSource=@($im.source_colors|ForEach-Object {[Drawing.ColorTranslator]::FromHtml('#'+$_.TrimStart('#')).ToArgb()})}
    else {
     $taskGray=@(-16777216,-11184811,-5592406,-1)
     $taskAllGray=$true;foreach($col in $taskColors){if($col -notin $taskGray){$taskAllGray=$false}}
     $taskSource=if($taskAllGray){$taskGray}else{@($taskColors)}
     while($taskSource.Count -lt 4){$taskSource+=@($taskSource[-1])}
    }
    if($taskSource.Count -ne 4){throw 'source_colors requires exactly four entries.'}
    foreach($col in $taskSource) {$c=[Drawing.Color]::FromArgb($col);$taskPalette+=@((($c.R-band 248)-shl 8)-bor (($c.G-band 252)-shl 3)-bor ($c.B-shr 3))}
    if($im.palette) {if($im.palette.Count -ne 4){throw 'palette requires four RGB565 colors.'};$taskPalette=@($im.palette|ForEach-Object {[Convert]::ToUInt16($_.TrimStart('#'),16)})}
    $taskStride=[int][Math]::Ceiling($taskBmp.Width/4.0);$taskBytes=[byte[]]::new($taskStride*$taskBmp.Height)
    for($y=0;$y -lt $taskBmp.Height;$y++){for($x=0;$x -lt $taskBmp.Width;$x++){
     $c=$taskBmp.GetPixel($x,$y);$best=0;$dist=[int]::MaxValue
     for($k=0;$k -lt 4;$k++) {$d=[Drawing.Color]::FromArgb($taskSource[$k]);$n=[int]([Math]::Pow($c.R-$d.R,2)+[Math]::Pow($c.G-$d.G,2)+[Math]::Pow($c.B-$d.B,2));if($n -lt $dist){$dist=$n;$best=$k}}
     $at=$y*$taskStride+[int][Math]::Floor($x/4.0);$taskBytes[$at]=$taskBytes[$at]-bor ($best-shl (6-2*($x%4)))
    }}
   } else {
    $taskStride=$taskBmp.Width*2;$taskBytes=[byte[]]::new($taskStride*$taskBmp.Height)
    for($y=0;$y -lt $taskBmp.Height;$y++){for($x=0;$x -lt $taskBmp.Width;$x++){
     $c=$taskBmp.GetPixel($x,$y);$v=(($c.R-band 248)-shl 8)-bor (($c.G-band 252)-shl 3)-bor ($c.B-shr 3);$at=$y*$taskStride+$x*2
     $taskBytes[$at]=$v-band 255;$taskBytes[$at+1]=$v-shr 8
    }}
    $taskPalette=@(0,0,0,0)
   }
   $taskLines.Add(('static const uint8_t {0}_bytes[] ASSET __attribute__((aligned(32))) = {{' -f $taskSymbol))
   for($at=0;$at -lt $taskBytes.Length;$at+=24){$end=[Math]::Min($at+23,$taskBytes.Length-1);$taskLines.Add(' '+(($taskBytes[$at..$end]|ForEach-Object {'0x{0:X2}'-f $_})-join ',')+',')}
   $taskLines.Add('};')
   $taskType=if($taskMode -eq '2bpp'){'HmiImage2'}elseif($taskMode -eq 'i2blend4'){'HmiImage2Aa'}else{'HmiRgbImage'}
   $taskKind=if($taskMode -eq '2bpp'){'HMI_PAGE_IMAGE2'}elseif($taskMode -eq 'i2blend4'){'HMI_PAGE_IMAGE2_AA'}else{'HMI_PAGE_RGB565'}
   $taskLines.Add(('static const {0} {1} ASSET = {{{1}_bytes,sizeof({1}_bytes),{2},{3},{4}}};' -f $taskType,$taskSymbol,$taskBmp.Width,$taskBmp.Height,$taskStride))
   $taskPaletteC='{'+(($taskPalette|ForEach-Object {'0x{0:X4}'-f $_})-join ',')+'}'
   $taskSelectedC=$taskPaletteC
   if($im.selected_palette){if($im.selected_palette.Count -ne 4){throw 'selected_palette requires four colors'};$taskSelectedC='{'+(($im.selected_palette|ForEach-Object {Color $_})-join ',')+'}'}
   $taskTarget=if($im.target_page){if(!$taskIds.ContainsKey($im.target_page)){throw 'Unknown target_page'};CQuote $im.target_page}else{'NULL'}
   $taskWidgetDefs.Add(('{{ .id={0},.kind={1},.bounds={2},.resource=&{3},.colors={4},.selected_colors={6},.target_page={5} }},' -f (CQuote $im.id),$taskKind,$taskBounds,$taskSymbol,$taskPaletteC,$taskTarget,$taskSelectedC))
   $taskReport+=@([ordered]@{page=$p.id;id=$im.id;symbol=($taskSymbol+'_bytes');source=$taskInput;format=$taskMode;width=$taskBmp.Width;height=$taskBmp.Height;stride=$taskStride;bytes=$taskBytes.Length;x=$taskX;y=$taskY;palette=$taskPalette;roles=@('background','border','title','graphic')})
  } finally {$taskBmp.Dispose()}
 }
 foreach($w in $p.widgets) {
  if($p.image -and $w.id -eq 'background'){throw 'background is reserved for the page PNG.'}
  if(!$w.id -or $taskWidgetIds.ContainsKey($w.id)){throw 'Widget IDs must be unique and nonempty within a page.'};$taskWidgetIds[$w.id]=$true
  if($w.image){continue}
  $bounds=if($p.coordinate_space -eq 'image'){Rect ([pscustomobject]@{id=$w.id;x=$w.x+$taskOriginX;y=$w.y+$taskOriginY;width=$w.width;height=$w.height})}else{Rect $w}
  $binding=if($w.binding){'HMI_BIND_'+$w.binding}else{'HMI_BIND_NONE'}
  if($binding -notin @('HMI_BIND_NONE','HMI_BIND_TC1','HMI_BIND_TC2','HMI_BIND_CO','HMI_BIND_CO2','HMI_BIND_STATE','HMI_BIND_MODE','HMI_BIND_TARGET','HMI_BIND_SOURCE','HMI_BIND_FAULT','HMI_BIND_DAMPER','HMI_BIND_AIR','HMI_BIND_SAMPLE','HMI_BIND_MODE_MANUAL','HMI_BIND_MODE_AUTO','HMI_BIND_RUN_STATE','HMI_BIND_STOP_STATE','HMI_BIND_GREENHOUSE','HMI_BIND_EXHAUST','HMI_BIND_CO_STATUS','HMI_BIND_CO_VALUE','HMI_BIND_CO2_VALUE','HMI_BIND_TARGET_VALUE','HMI_BIND_CO2_STATUS','HMI_BIND_FAULT_STATUS')) {throw ('Unknown binding: '+$w.binding)}
  $target=if($w.target_page){if(!$taskIds.ContainsKey($w.target_page)){throw 'Unknown target_page'};CQuote $w.target_page}else{'NULL'}
  $align=if($w.align){'HMI_TEXT_'+$w.align.ToUpperInvariant()}else{'HMI_TEXT_LEFT'}
  if($align -notin @('HMI_TEXT_LEFT','HMI_TEXT_CENTER','HMI_TEXT_RIGHT')){throw 'Invalid text alignment'}
  $text=if($w.text){CQuote $w.text}else{'""'};$opaque=if($w.opaque){'true'}else{'false'}
  $kind=if(!$w.type -or $w.type -eq 'text'){'HMI_PAGE_TEXT'}elseif($w.type -eq 'badge'){'HMI_PAGE_BADGE'}else{throw 'Widget type must be text or badge.'}
  $font=if(!$w.font -or $w.font -eq 'normal'){'NULL'}elseif($w.font -eq 'small'){'&home_small_font'}elseif($w.font -eq 'value'){'&home_value_font'}else{throw 'Font must be normal, small or value.'}
  $radius=if($null -eq $w.radius){6}else{[int]$w.radius};if($radius -lt 0 -or $radius -gt 32){throw 'Badge radius must be 0..32.'}
  $taskWidgetDefs.Add(('{{ .id={0},.kind={9},.bounds={1},.resource={2},.binding={3},.target_page={4},.foreground={5},.background={6},.align={7},.opaque={8},.font={10},.radius={11},.selected_colors={{0x19c8,0,0,0}} }},' -f (CQuote $w.id),$bounds,$text,$binding,$target,(Color $w.color),(Color $w.background '0841'),$align,$opaque,$kind,$font,$radius))
 }
 if($taskWidgetDefs.Count -gt 128){throw 'Maximum 128 widgets per page.'}
 $taskLines.Add(('static const HmiWidget widgets_{0}[] ASSET = {{' -f $taskPageIndex));$taskLines.AddRange($taskWidgetDefs);$taskLines.Add('};')
 $taskPageDefs.Add(('{{ {0},{1},{2},widgets_{3} }},' -f (CQuote $p.id),(Color $p.background '0841'),$taskWidgetDefs.Count,$taskPageIndex));$taskPageIndex++
}
$taskLines.Add('const HmiPage hmi_pages[] ASSET = {');$taskLines.AddRange($taskPageDefs);$taskLines.Add('};')
$taskLines.Add(('const uint16_t hmi_page_count = {0};' -f $taskPageIndex))
[IO.File]::WriteAllLines((Join-Path $taskOut 'pages.c'),$taskLines)
[IO.File]::WriteAllText((Join-Path $taskOut 'pages-report.json'),($taskReport|ConvertTo-Json -Depth 8))
Write-Output ('Validated and packed '+$taskPageIndex+' pages; '+$taskReport.Count+' PNG assets.')

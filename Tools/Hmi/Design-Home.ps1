param([string]$OutputDirectory=(Join-Path $PSScriptRoot '../../Assets'),[switch]$PwmTest)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
if(!('HmiPanelRaster' -as [type])){Add-Type -TypeDefinition ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'PanelRaster.cs'))) -ReferencedAssemblies System.Drawing}
$taskOut=[IO.Path]::GetFullPath($OutputDirectory);[IO.Directory]::CreateDirectory($taskOut)|Out-Null
$bmp=[Drawing.Bitmap]::new(1920,1280)
$g=[Drawing.Graphics]::FromImage($bmp)
$g.ScaleTransform(4,4)
$g.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::None
$g.TextRenderingHint=[Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
# Semantic source indices, not display colors: 0 background, 1 border,
# 2 static title/text, 3 graphic. Never quantize a gradient to infer these roles.
function Col([string]$hex) {return [Drawing.Color]::White}
function Role([int]$index) {return [Drawing.Color]::FromArgb(255,85*$index,85*$index,85*$index)}
function Color565([string]$hex) {
 $c=[Drawing.ColorTranslator]::FromHtml('#'+$hex)
 return '{0:X4}' -f ((($c.R -band 248) -shl 8) -bor (($c.G -band 252) -shl 3) -bor ($c.B -shr 3))
}
function RoundPath([float]$x,[float]$y,[float]$w,[float]$h,[float]$r) {
 $p=[Drawing.Drawing2D.GraphicsPath]::new();$d=2*$r
 $p.AddArc($x,$y,$d,$d,180,90);$p.AddArc($x+$w-$d,$y,$d,$d,270,90)
 $p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90);$p.AddArc($x,$y+$h-$d,$d,$d,90,90);$p.CloseFigure();return $p
}
function Panel([float]$x,[float]$y,[float]$w,[float]$h,[string]$top='092C49',[string]$bottom='04192C',[string]$edge='235476',[float]$r=7) {
 $p=RoundPath $x $y $w $h $r
 $br=[Drawing.SolidBrush]::new((Role 0))
 $pen=[Drawing.Pen]::new((Role 1),1)
 try{$g.FillPath($br,$p);$g.DrawPath($pen,$p)}finally{$p.Dispose();$br.Dispose();$pen.Dispose()}
}
function Txt([string]$text,[float]$x,[float]$y,[float]$size=10,[string]$color='E4F0FA',[bool]$bold=$true) {
 $style=if($bold){[Drawing.FontStyle]::Bold}else{[Drawing.FontStyle]::Regular}
 $font=[Drawing.Font]::new('Segoe UI',$size,$style,[Drawing.GraphicsUnit]::Pixel)
 $brush=[Drawing.SolidBrush]::new((Role 2));$fmt=[Drawing.StringFormat]::GenericTypographic.Clone()
 try{$g.DrawString($text,$font,$brush,[Drawing.PointF]::new($x,$y),$fmt)}finally{$font.Dispose();$brush.Dispose();$fmt.Dispose()}
}
function Line([float]$x1,[float]$y1,[float]$x2,[float]$y2,[string]$color='557F9B',[float]$width=1) {
 $pen=[Drawing.Pen]::new((Col $color),$width);try{$g.DrawLine($pen,$x1,$y1,$x2,$y2)}finally{$pen.Dispose()}
}
function Leaf([float]$x,[float]$y,[float]$w,[float]$h,[string]$color='64D942') {
 $p=[Drawing.Drawing2D.GraphicsPath]::new()
 $p.AddBezier($x,$y+$h,$x-$w*.10,$y+$h*.20,$x+$w*.4,$y,$x+$w,$y)
 $p.AddBezier($x+$w,$y,$x+$w*.93,$y+$h*.75,$x+$w*.45,$y+$h*1.08,$x,$y+$h)
 $b=[Drawing.SolidBrush]::new((Col $color));try{$g.FillPath($b,$p)}finally{$b.Dispose();$p.Dispose()}
 Line ($x-1) ($y+$h+2) ($x+$w*.73) ($y+$h*.24) '236A44' 1
}
function Gear([float]$x,[float]$y) {
 $pen=[Drawing.Pen]::new((Col 'D9EAF5'),2)
 try {
  $g.DrawEllipse($pen,$x+3,$y+3,12,12);$g.DrawEllipse($pen,$x+7,$y+7,4,4)
  for($i=0;$i -lt 8;$i++){$a=$i*[Math]::PI/4;$g.DrawLine($pen,[float]($x+9+6*[Math]::Cos($a)),[float]($y+9+6*[Math]::Sin($a)),[float]($x+9+9*[Math]::Cos($a)),[float]($y+9+9*[Math]::Sin($a)))}
 }finally{$pen.Dispose()}
}
function Power([float]$x,[float]$y) {
 $pen=[Drawing.Pen]::new((Col 'E8F5FF'),2);$pen.StartCap=$pen.EndCap=[Drawing.Drawing2D.LineCap]::Round
 try{$g.DrawArc($pen,$x,$y+1,14,14,-52,284);$g.DrawLine($pen,$x+7,$y-1,$x+7,$y+7)}finally{$pen.Dispose()}
}
function Flame([float]$x,[float]$y,[float]$w=16,[float]$h=22) {
 $p=[Drawing.Drawing2D.GraphicsPath]::new()
 $p.AddBezier($x+$w*.52,$y,$x+$w*.72,$y+$h*.37,$x+$w*.17,$y+$h*.35,$x+$w*.38,$y+$h*.67)
 $p.AddBezier($x+$w*.38,$y+$h*.67,$x+$w*.55,$y+$h*.52,$x+$w*.72,$y+$h*.43,$x+$w*.7,$y+$h*.3)
 $p.AddBezier($x+$w*.7,$y+$h*.3,$x+$w*1.35,$y+$h*.85,$x+$w*.71,$y+$h*1.13,$x+$w*.44,$y+$h)
 $p.AddBezier($x+$w*.44,$y+$h,$x-$w*.36,$y+$h*.74,$x+$w*.11,$y+$h*.33,$x+$w*.52,$y)
 $b=[Drawing.SolidBrush]::new((Col 'FFB23F'));try{$g.FillPath($b,$p)}finally{$b.Dispose();$p.Dispose()}
}
function House([float]$x,[float]$y) {
 Line $x ($y+10) ($x+20) $y '95CEB8' 1.6;Line ($x+20) $y ($x+40) ($y+10) '95CEB8' 1.6
 Line ($x+3) ($y+9) ($x+3) ($y+25) '95CEB8' 1.4;Line ($x+37) ($y+9) ($x+37) ($y+25) '95CEB8' 1.4
 Line ($x+3) ($y+25) ($x+37) ($y+25) '95CEB8' 1.4
 Line ($x+13) ($y+7) ($x+13) ($y+22) '95CEB8' .7;Line ($x+27) ($y+7) ($x+27) ($y+22) '95CEB8' .7
 Leaf ($x+13) ($y+11) 16 12 '77D965'
}
function Stack([float]$x,[float]$y) {
 $b=[Drawing.SolidBrush]::new((Col '9CBBD3'))
 try{$g.FillRectangle($b,$x+9,$y+9,12,15);$g.FillRectangle($b,$x+7,$y+7,16,3);$g.FillEllipse($b,$x+17,$y,10,5);$g.FillEllipse($b,$x+22,$y-3,9,5)}finally{$b.Dispose()}
}
try {
 $g.Clear((Role 0))
 $bg=[Drawing.SolidBrush]::new((Role 0))
 $g.FillRectangle($bg,0,0,480,320);$bg.Dispose()
 Panel 4 3 472 44 '083958' '05243E' '123F5E' 8
 Txt 'AERONIX' 13 9 23 'F3FAFF';Leaf 132 5 15 9
 Line 174 10 174 35 '356981' 1
 Txt 'CO2 GENERATOR' 184 10 14;Txt 'GREENHOUSE CONTROL' 185 29 9 'A7CAD9' $false
 # A quiet wireframe greenhouse replaces the reference photo at native size.
 for($i=0;$i -lt 4;$i++) {
  $x=366+$i*24
  Line $x 40 $x 21 '245A70';Line $x 21 ($x+12) 12 '245A70';Line ($x+12) 12 ($x+24) 21 '245A70';Line ($x+24) 21 ($x+24) 40 '245A70'
 }
 Line 366 27 462 27 '245A70';Line 366 35 462 35 '245A70'
 $accent=[Drawing.Drawing2D.GraphicsPath]::new();$accent.AddLine(9,45,266,45);$accent.AddBezier(266,45,286,45,294,36,304,29)
 $pen=[Drawing.Pen]::new((Col '5BCB4C'),2);$g.DrawPath($pen,$accent);$pen.Dispose();$accent.Dispose()

 Panel 8 52 180 60;Gear 18 59;Txt 'OPERATING MODE' 41 60 11
 Panel 14 76 82 30 '163D55' '09243A' '37768F' 7
 Panel 99 76 82 30 '163D55' '09243A' '37768F' 7
 Panel 196 52 276 60;Power 207 61;Txt 'DEVICE STATE' 229 60 11
 Panel 200 76 130 30 '163D55' '09243A' '37768F' 7
 Panel 336 76 130 30 '163D55' '09243A' '37768F' 7

 Panel 8 116 464 22 '08253B' '071D31' '153F58' 4
 Flame 17 120 10 14;Txt 'FLAME' 33 121 9 '96B8CD'
 Line 218 121 218 133 '365A71'
 Txt 'SAMPLE' 254 121 9 '96B8CD'
 Line 237 120 237 134 '53BBDD' 1.1;Line 231 123 243 131 '53BBDD' 1.1;Line 231 131 243 123 '53BBDD' 1.1

 Panel 8 144 218 96;Flame 19 150 19 25;Txt 'COMBUSTION CO' 46 152 12
 Panel 18 177 116 42 '031A2B' '031422' '225C7D' 7
 Txt 'ppm' 111 198 9 'BCD0DE' $false
 Txt 'QUALITY' 154 169 9 '92B3C8'
 Panel 145 183 71 28 '18384C' '0B2539' '38647B' 7
 Line 21 228 211 228 '1A5370' 2;Line 21 227 104 227 '2186A4' 1

 Panel 234 144 238 96
 Line 245 152 256 152 'D2E4EF' 1.3;Line 245 163 256 163 'D2E4EF' 1.3
 Line 245 152 245 163 'D2E4EF' 1.3;Line 256 152 256 163 'D2E4EF' 1.3
 Line 247 156 254 156 'D2E4EF';Line 247 160 254 160 'D2E4EF'
 Txt 'DAMPERS' 264 152 12
 Panel 242 168 111 65 '07392E' '06251F' '31795A' 6
 Panel 357 168 107 65 '12334D' '0B253E' '416A88' 6
 Txt 'GREENHOUSE' 264 172 9 'C2E4D7';Txt 'EXHAUST' 389 172 9 'C7D7E5'
 House 277 183;Stack 393 183

 Panel 8 246 218 56 '083552' '05213A' '277398' 7
 Panel 234 246 238 56 '073A34' '042723' '33815B' 7
 $targetTitle=if($PwmTest){'PWM TEST / CCR1'}else{'CO2 TARGET'}
 Txt $targetTitle 19 252 10 'D2E9F5'
 $pen=[Drawing.Pen]::new((Col '4EBAE7'),1.5)
 $g.DrawEllipse($pen,20,270,17,17);$g.DrawEllipse($pen,25,275,7,7);$pen.Dispose()
 Line 28 267 28 272 '4EBAE7';Line 28 285 28 290 '4EBAE7';Line 17 279 22 279 '4EBAE7';Line 35 279 40 279 '4EBAE7'
 $targetUnit=if($PwmTest){'ticks'}else{'ppm'}
 Txt $targetUnit 178 282 10 'BDD6E5' $false
 Txt 'GREENHOUSE CO2' 246 252 10 'D6EFDC';Leaf 250 274 17 13
 Txt 'ppm' 399 282 10 'C5DECE' $false
 $bmp.Save((Join-Path $taskOut 'home-indices.png'),[Drawing.Imaging.ImageFormat]::Png)
 # Each panel is a complete, opaque 2bpp asset. Parent panels are drawn first;
 # independent nested panels then cover their own rectangles completely.
 $panels=@(
  @('header',4,3,473,45),@('mode_panel',8,52,181,61),
  @('manual_frame',14,76,83,31),@('auto_frame',99,76,83,31),
  @('state_panel',196,52,277,61),@('run_frame',200,76,131,31),@('stop_frame',336,76,131,31),
  @('temperature_panel',8,116,465,23),
  @('co_panel',8,144,219,97),@('co_value_frame',18,177,117,43),@('co_quality_frame',145,183,72,29),
  @('damper_panel',234,144,239,97),@('greenhouse_frame',242,168,112,66),@('exhaust_frame',357,168,108,66),
  @('target_panel',8,246,219,57),@('co2_panel',234,246,239,57),@('footer_panel',0,304,480,16)
 )
 $panelDir=Join-Path $taskOut 'panels';[IO.Directory]::CreateDirectory($panelDir)|Out-Null
 $removedPanel=Join-Path $panelDir 'fault_panel.png'
 if(Test-Path -LiteralPath $removedPanel -PathType Leaf){Remove-Item -LiteralPath $removedPanel -Force}
 $panelWidgets=@()
 foreach($panel in $panels) {
  $crop=[HmiPanelRaster]::Pack($bmp,$panel[1],$panel[2],$panel[3],$panel[4],4)
  try{$crop.Save((Join-Path $panelDir ($panel[0]+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$crop.Dispose()}
  $colors=@('051B32','235C82','EAF5FF','5ACCE8')
  if($panel[0] -eq 'header'){$colors=@('062B4A','1C5D83','F4FAFF','70D954')}
  if($panel[0] -eq 'co_panel'){$colors=@('04172C','2581AC','EAF5FF','FFAE32')}
  if($panel[0] -eq 'co2_panel'){$colors=@('032D30','258771','EAF9F3','79F077')}
  if($panel[0] -eq 'greenhouse_frame'){$colors=@('053427','349D65','E7FFF1','87F779')}
  if($panel[0] -eq 'exhaust_frame'){$colors=@('0A2541','527DA5','E7F2FF','C3DDF6')}
  if($panel[0] -eq 'target_panel'){$colors=@('052C4A','3099CF','ECF7FF','61D0F3')}
  if($panel[0] -eq 'temperature_panel'){$colors=@('061E32','255771','CEE8F6','69CDDD')}
  $palette=@($colors|ForEach-Object {Color565 $_})
  $panelWidgets+=@(@{id=$panel[0];image=('panels/'+$panel[0]+'.png');format='i2blend4';x=$panel[1];y=$panel[2];
    source_colors=@('000000','555555','AAAAAA','FFFFFF');palette=$palette})
 }
} finally {$g.Dispose();$bmp.Dispose()}

# Geometry below is the dynamic layer. The PNG deliberately contains no
# measurements, mode selection, OPEN/CLOSED assertions, targets or fault state.
$widgets=@(
 @{id='manual';binding='MODE_MANUAL';type='badge';font='small';x=16;y=78;width=78;height=27;align='center';radius=5},
 @{id='auto';binding='MODE_AUTO';type='badge';font='small';x=101;y=78;width=78;height=27;align='center';radius=5},
 @{id='run';binding='RUN_STATE';type='badge';font='small';x=202;y=78;width=126;height=27;align='center';radius=5},
 @{id='stop';binding='STOP_STATE';type='badge';font='small';x=338;y=78;width=126;height=27;align='center';radius=5},
 @{id='tc1';binding='TC1';x=78;y=117;width=131;height=20;color='E73F'},
 @{id='tc2';binding='TC2';x=313;y=117;width=150;height=20;color='E73F'},
 @{id='co';binding='CO_VALUE';font='value';x=22;y=180;width=85;height=31;align='center'},
 @{id='co_quality';binding='CO_STATUS';type='badge';font='small';x=148;y=185;width=66;height=24;align='center';radius=5},
 @{id='greenhouse';binding='GREENHOUSE';type='badge';font='small';x=247;y=210;width=101;height=21;align='center';radius=5},
 @{id='exhaust';binding='EXHAUST';type='badge';font='small';x=361;y=210;width=99;height=21;align='center';radius=5},
 @{id='target';binding='TARGET_VALUE';font='value';x=47;y=268;width=123;height=31;align='center';color='FFFF'},
 @{id='co2';binding='CO2_VALUE';font='value';x=273;y=268;width=119;height=31;align='center'},
 @{id='co2_status';binding='CO2_STATUS';font='small';x=405;y=250;width=61;height=15;align='right'},
 @{id='source';binding='SOURCE';font='small';x=10;y=305;width=295;height=14;color='94F7'}
)
$ui=[ordered]@{schema=1;display=[ordered]@{width=480;height=320;orientation='landscape'};pages=@([ordered]@{id='home';background='0841';widgets=@($panelWidgets)+@($widgets)})}
[IO.File]::WriteAllText((Join-Path $taskOut 'ui.json'),($ui|ConvertTo-Json -Depth 10))
Write-Output 'Designed native 480x320 Home: 4x supersampled artwork, 17 independent four-role panels + coverage + live fields.'

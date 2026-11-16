param([Parameter(Mandatory=$true)][string]$ProfileDirectory,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$taskReport=Get-Content -LiteralPath (Join-Path $ProfileDirectory 'pages-report.json') -Raw|ConvertFrom-Json
$taskBlob=[IO.File]::ReadAllBytes((Join-Path $ProfileDirectory 'External_Assets.bin'))
$taskOut=[IO.Path]::GetFullPath($OutputDirectory);[IO.Directory]::CreateDirectory($taskOut)|Out-Null
foreach($suffix in @('.2bpp','.i2a4','.roles.2bpp','.base.2bpp','.coverage.4bpp')) {
 $removed=Join-Path $taskOut ('fault_panel'+$suffix)
 if(Test-Path -LiteralPath $removed -PathType Leaf){Remove-Item -LiteralPath $removed -Force}
}
$taskHeader=[Collections.Generic.List[string]]::new()
$taskHeader.Add('/* Generated from the actual linked ELF. Do not guess offsets. */')
$taskHeader.Add('#ifndef HOME_PANEL_MAP_H');$taskHeader.Add('#define HOME_PANEL_MAP_H')
$taskHeader.Add('#define HOME_PANEL_FLASH_BASE 0x90000000UL')
$taskHeader.Add('/* Palette roles: 0 background, 1 border, 2 title, 3 graphic. */')
$taskRanges=@();$taskMap=@()
foreach($panel in $taskReport) {
 if($panel.page -ne 'home'){continue}
 if($panel.format -notin @('2bpp','i2blend4') -or $panel.id -match 'reset'){throw 'Home must use separate four-role assets without a reset control.'}
 if($panel.id -notmatch '^[a-z][a-z0-9_]*$'){throw 'Invalid panel identifier.'}
 $offset=[Convert]::ToInt32($panel.flash_offset.Substring(2),16)
 $address=[Convert]::ToInt64($panel.address.Substring(2),16)
 $length=[int]$panel.bytes
 if($address -ne 0x90000000L+$offset -or $offset%32 -or $offset -lt 256 -or $offset+$length -gt $taskBlob.Length){throw 'Invalid panel address/range.'}
 $expectedStride=if($panel.format -eq 'i2blend4'){$panel.width}else{[Math]::Ceiling($panel.width/4.0)}
 if($length -ne $panel.stride*$panel.height -or $panel.stride -ne $expectedStride -or $panel.palette.Count -ne 4){throw 'Invalid panel layout.'}
 foreach($range in $taskRanges){if($offset -lt $range.end -and $offset+$length -gt $range.start){throw 'Panel flash ranges overlap.'}}
 $taskRanges+=@([pscustomobject]@{start=$offset;end=$offset+$length})
 $data=[byte[]]::new($length);[Array]::Copy($taskBlob,$offset,$data,0,$length)
 $filename=$panel.id+$(if($panel.format -eq 'i2blend4'){'.i2a4'}else{'.2bpp'});[IO.File]::WriteAllBytes((Join-Path $taskOut $filename),$data)
 if($panel.format -eq 'i2blend4') {
  $legacy=Join-Path $taskOut ($panel.id+'.2bpp')
  if(Test-Path -LiteralPath $legacy -PathType Leaf){Remove-Item -LiteralPath $legacy -Force}
  $roleStride=[int][Math]::Ceiling($panel.width/4.0);$maskStride=[int][Math]::Ceiling($panel.width/2.0)
  $roles=[byte[]]::new($roleStride*$panel.height);$bases=[byte[]]::new($roles.Length);$mask=[byte[]]::new($maskStride*$panel.height)
  for($y=0;$y -lt $panel.height;$y++){for($x=0;$x -lt $panel.width;$x++){
   $v=$data[$y*$panel.stride+$x];$at=$y*$roleStride+[int][Math]::Floor($x/4.0);$shift=6-2*($x%4)
   $roles[$at]=$roles[$at] -bor (($v -shr 6) -shl $shift);$bases[$at]=$bases[$at] -bor ((($v -shr 4) -band 3) -shl $shift)
   $at=$y*$maskStride+[int][Math]::Floor($x/2.0);$mask[$at]=$mask[$at] -bor (($v -band 15) -shl (4-4*($x%2)))
  }}
  [IO.File]::WriteAllBytes((Join-Path $taskOut ($panel.id+'.roles.2bpp')),$roles)
  [IO.File]::WriteAllBytes((Join-Path $taskOut ($panel.id+'.base.2bpp')),$bases)
  [IO.File]::WriteAllBytes((Join-Path $taskOut ($panel.id+'.coverage.4bpp')),$mask)
 }
 $key='HOME_PANEL_'+$panel.id.ToUpperInvariant()
 $taskHeader.Add(('#define {0}_OFFSET {1}UL' -f $key,$panel.flash_offset))
 $taskHeader.Add(('#define {0}_ADDRESS {1}UL' -f $key,$panel.address))
 foreach($field in @('width','height','stride','bytes')){$taskHeader.Add(('#define {0}_{1} {2}U' -f $key,$field.ToUpperInvariant(),$panel.$field))}
 $taskMap+=@([pscustomobject][ordered]@{id=$panel.id;format=$panel.format;x=$panel.x;y=$panel.y;width=$panel.width;height=$panel.height;stride=$panel.stride;bytes=$length;flash_offset=$panel.flash_offset;address=$panel.address;background=('0x{0:X4}'-f $panel.palette[0]);border=('0x{0:X4}'-f $panel.palette[1]);title=('0x{0:X4}'-f $panel.palette[2]);graphic=('0x{0:X4}'-f $panel.palette[3]);file=$filename;sha256=(Get-FileHash -LiteralPath (Join-Path $taskOut $filename) -Algorithm SHA256).Hash})
}
if($taskMap.Count -ne 17){throw 'Expected 17 independent Home panels.'}
$taskHeader.Add('#endif');[IO.File]::WriteAllLines((Join-Path $taskOut 'home-panel-map.h'),$taskHeader)
$taskMap|Export-Csv -LiteralPath (Join-Path $taskOut 'home-panel-map.csv') -NoTypeInformation -Encoding UTF8
[IO.File]::WriteAllText((Join-Path $taskOut 'home-panel-map.json'),($taskMap|ConvertTo-Json -Depth 6))
Copy-Item -LiteralPath (Join-Path $ProfileDirectory 'pages.c') -Destination (Join-Path $taskOut 'home-pages-generated.c') -Force
Write-Host ('PASS '+$taskMap.Count+' separate four-role panels; actual aligned offsets, nonoverlapping ranges and extracted bytes verified.')

param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=Join-Path $taskRoot 'Debug/FinalV1'
$taskSources=@('Libraries/Hmi/src/HmiTypes.c','Libraries/Hmi/src/HmiImage.c','Libraries/Hmi/src/HmiText.c','Libraries/Hmi/src/HmiPages.c','Libraries/Hmi/src/HmiHome.c','Libraries/Hmi/src/HmiDiagData.c','Core/Src/SensorMath.c','Core/Src/RtuMaster.c','Core/Src/BurnerControl.c','Core/Src/CombustionCycle.c')|ForEach-Object {Join-Path $taskRoot $_}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Libraries/Hmi/include') -I (Join-Path $taskRoot 'Core/Inc') @taskSources (Join-Path $taskOut 'pages.c') (Join-Path $taskOut 'font.c') (Join-Path $taskOut 'font-small.c') (Join-Path $taskOut 'font-value.c') (Join-Path $PSScriptRoot 'final.c') -lm -o (Join-Path $taskOut 'final-test.exe')
if($LASTEXITCODE -ne 0){throw 'Integrated pure tests compile failed'}
& (Join-Path $taskOut 'final-test.exe') (Join-Path $taskOut 'home-preview.rgb565')
if($LASTEXITCODE -ne 0){throw 'Integrated pure tests failed'}
$taskDmaSources=@('HmiTypes','HmiImage','HmiText','HmiPages','HmiPagesDma','HmiHome','HmiStream')|ForEach-Object {Join-Path $taskRoot ('Libraries/Hmi/src/'+$_+'.c')}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Libraries/Hmi/include') @taskDmaSources (Join-Path $taskOut 'pages.c') (Join-Path $taskOut 'font.c') (Join-Path $taskOut 'font-small.c') (Join-Path $taskOut 'font-value.c') (Join-Path $PSScriptRoot 'dma.c') -o (Join-Path $taskOut 'dma-test.exe')
if($LASTEXITCODE -ne 0){throw 'DMA pipeline tests compile failed'}
& (Join-Path $taskOut 'dma-test.exe')
if($LASTEXITCODE -ne 0){throw 'DMA pipeline tests failed'}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -DHMI_TEST_TILE_ROWS=16U -I (Join-Path $taskRoot 'Libraries/Hmi/include') @taskDmaSources (Join-Path $taskOut 'pages.c') (Join-Path $taskOut 'font.c') (Join-Path $taskOut 'font-small.c') (Join-Path $taskOut 'font-value.c') (Join-Path $PSScriptRoot 'dma.c') -o (Join-Path $taskOut 'dma-16-row-test.exe')
if($LASTEXITCODE -ne 0){throw '16-row DMA pipeline tests compile failed'}
& (Join-Path $taskOut 'dma-16-row-test.exe')
if($LASTEXITCODE -ne 0){throw '16-row DMA pipeline tests failed'}

param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskOut=Join-Path $taskRoot 'Debug/Hmi'
New-Item -ItemType Directory -Force $taskOut | Out-Null
$taskSources=@('HmiTypes','HmiImage','HmiText','HmiStream') | ForEach-Object {Join-Path $taskRoot ('Libraries/Hmi/src/'+$_+'.c')}
& $Compiler -std=c11 -O2 -g -Wall -Wextra -Werror -Wconversion -Wsign-conversion -I (Join-Path $taskRoot 'Libraries/Hmi/include') @taskSources (Join-Path $PSScriptRoot 'test.c') -o (Join-Path $taskOut 'hmi-test.exe')
if ($LASTEXITCODE -ne 0) {throw 'HMI tests compile failed'}
& (Join-Path $taskOut 'hmi-test.exe')
if ($LASTEXITCODE -ne 0) {throw 'HMI tests failed'}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $PSScriptRoot 'mock') -I (Join-Path $taskRoot 'Libraries/Hmi/include') -I (Join-Path $taskRoot 'Libraries/Hmi/platform') (Join-Path $taskRoot 'Libraries/Hmi/platform/HmiSt7796.c') (Join-Path $PSScriptRoot 'platform.c') -o (Join-Path $taskOut 'hmi-platform-test.exe')
if ($LASTEXITCODE -ne 0) {throw 'HMI platform tests compile failed'}
& (Join-Path $taskOut 'hmi-platform-test.exe')
if ($LASTEXITCODE -ne 0) {throw 'HMI platform tests failed'}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -include (Join-Path $PSScriptRoot 'asset-mock.h') -I (Join-Path $taskRoot 'Core/Inc') -I (Join-Path $taskRoot 'Libraries/Hmi/include') -I (Join-Path $taskRoot 'Libraries/Hmi/platform') (Join-Path $taskRoot 'Libraries/Hmi/platform/HmiAssetDma.c') (Join-Path $PSScriptRoot 'asset-platform.c') -o (Join-Path $taskOut 'hmi-asset-platform-test.exe')
if($LASTEXITCODE -ne 0){throw 'Asset DMA platform tests compile failed'}
& (Join-Path $taskOut 'hmi-asset-platform-test.exe')
if($LASTEXITCODE -ne 0){throw 'Asset DMA platform tests failed'}
$taskDiagSources=@('Core/Src/DiagLog.c','Core/Src/KeyInput.c','Libraries/Hmi/src/HmiDiag.c','Libraries/Hmi/src/HmiDiagData.c','Libraries/Hmi/src/HmiText.c','Libraries/Hmi/src/HmiTypes.c')|ForEach-Object {Join-Path $taskRoot $_}
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Libraries/Hmi/include') -I (Join-Path $taskRoot 'Core/Inc') @taskDiagSources (Join-Path $PSScriptRoot 'diag.c') -o (Join-Path $taskOut 'hmi-diag-test.exe')
if($LASTEXITCODE -ne 0) {throw 'DIAG tests compile failed'}
& (Join-Path $taskOut 'hmi-diag-test.exe')
if($LASTEXITCODE -ne 0) {throw 'DIAG tests failed'}

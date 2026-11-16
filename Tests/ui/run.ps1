param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$taskExe=Join-Path $PSScriptRoot 'ui-test.exe'
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Core/Inc') `
 (Join-Path $taskRoot 'Core/Src/KeyInput.c') (Join-Path $taskRoot 'Core/Src/UiNavigation.c') `
 (Join-Path $taskRoot 'Core/Src/DiagLog.c') (Join-Path $taskRoot 'Core/Src/BurnerControl.c') (Join-Path $taskRoot 'Core/Src/CombustionCycle.c') (Join-Path $PSScriptRoot 'test.c') -o $taskExe
if ($LASTEXITCODE -ne 0) {throw 'UI logic test compilation failed'}
& $taskExe
if ($LASTEXITCODE -ne 0) {throw 'UI logic test failed'}

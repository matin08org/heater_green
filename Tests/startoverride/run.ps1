param([string]$Compiler = 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$taskExe = Join-Path $PSScriptRoot 'startoverride-test.exe'
& $Compiler -std=c11 -Wall -Wextra -Werror -I (Join-Path $taskRoot 'Core/Inc') `
    (Join-Path $taskRoot 'Core/Src/BurnerControl.c') (Join-Path $taskRoot 'Core/Src/CombustionCycle.c') `
    (Join-Path $taskRoot 'Core/Src/StartOverride.c') (Join-Path $PSScriptRoot 'test.c') -o $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Start override test compilation failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Start override tests failed' }

param([string]$Compiler='C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
foreach($taskCase in @(@(1,1),@(0,1),@(1,0))) {
 $taskExe=Join-Path $PSScriptRoot ('board-scenario-'+$taskCase[0]+'-'+$taskCase[1]+'.exe')
 & $Compiler -std=c11 -O2 -Wall -Wextra -Werror ('-DSIM_FLAME='+$taskCase[0]) ('-DSIM_CO='+$taskCase[1]) -I (Join-Path $taskRoot 'Core/Inc') `
 (Join-Path $taskRoot 'Core/Src/BurnerControl.c') (Join-Path $taskRoot 'Core/Src/CombustionCycle.c') (Join-Path $taskRoot 'Tests/scenario/BoardScenario.c') (Join-Path $PSScriptRoot 'board-test.c') -o $taskExe
 if($LASTEXITCODE -ne 0){throw 'Board scenario test compile failed'}
 & $taskExe
 if($LASTEXITCODE -ne 0){throw 'Board scenario test failed'}
}

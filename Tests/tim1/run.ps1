$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
 & 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror -include Tests/tim1/mock.h -ICore/Inc Core/Src/tim.c Tests/tim1/test.c -o Tests/tim1/test.exe
 if($LASTEXITCODE -ne 0){throw 'TIM1 phase test compile failed'}
 & ./Tests/tim1/test.exe
 if($LASTEXITCODE -ne 0){throw 'TIM1 phase tests failed'}
} finally {Pop-Location}

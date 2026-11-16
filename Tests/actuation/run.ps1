$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
 & 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror -include Tests/actuation/mock.h -ICore/Inc Core/Src/BoardActuation.c Tests/actuation/test.c -o Tests/actuation/test.exe
 if($LASTEXITCODE -ne 0){throw 'Actuation test compile failed'}
 & ./Tests/actuation/test.exe
 if($LASTEXITCODE -ne 0){throw 'Actuation tests failed'}
} finally {Pop-Location}

$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
 & 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror -include Tests/actuation/mock.h -ICore/Inc Core/Src/BoardActuation.c Tests/actuation/BoardOutputTest.c Tests/actuation/output-test.c -o Tests/actuation/output-test.exe
 if($LASTEXITCODE -ne 0){throw 'Output-test compilation failed'}
 & ./Tests/actuation/output-test.exe
 if($LASTEXITCODE -ne 0){throw 'Output-test assertions failed'}
} finally {Pop-Location}

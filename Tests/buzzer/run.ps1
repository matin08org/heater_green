$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
& 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -std=c11 -Wall -Wextra -Werror -include Tests/buzzer/mock.h -ICore/Inc Core/Src/Buzzer.c Tests/buzzer/test.c -o Tests/buzzer/test.exe
if ($LASTEXITCODE -ne 0) {throw 'Buzzer compilation failed'}
& ./Tests/buzzer/test.exe
if ($LASTEXITCODE -ne 0) {throw 'Buzzer regression failed'}
} finally {Pop-Location}

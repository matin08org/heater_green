$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
& 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -std=c11 -Wall -Wextra -Werror -include Tests/stepper/mock.h -ICore/Inc Core/Src/DamperStepper.c Tests/stepper/test.c -o Tests/stepper/test.exe
if ($LASTEXITCODE -ne 0) {throw 'Stepper compilation failed'}
& ./Tests/stepper/test.exe
if ($LASTEXITCODE -ne 0) {throw 'Stepper regression failed'}
} finally {Pop-Location}

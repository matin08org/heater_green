param([string]$Compiler = 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
    & $Compiler -std=c11 -Wall -Wextra -Werror -include Tests/max6675/mock.h -ICore/Inc Core/Src/HC595.c Core/Src/MAX6675.c Tests/max6675/test.c -o Tests/max6675/test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native test build failed.' }
    foreach ($scenario in @('normal','wrap','startup')) {
        & ./Tests/max6675/test.exe $scenario
        if ($LASTEXITCODE -ne 0) { throw "MAX6675 test failed: $scenario" }
    }
} finally { Pop-Location }

param([string]$Compiler='gcc')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $root
try {
    New-Item -ItemType Directory -Force Debug/radio-tests | Out-Null
    & $Compiler -std=c11 -O2 -Wall -Wextra -Werror -DNRF24_HOST_TEST -ICore/Inc Core/Src/NRF24L01.c Tests/radio/test.c -o Debug/radio-tests/radio_test.exe
    if($LASTEXITCODE -ne 0) { throw 'Host test build failed.' }
    & ./Debug/radio-tests/radio_test.exe
    if($LASTEXITCODE -ne 0) { throw 'Radio simulation failed.' }
    & ./Debug/radio-tests/radio_test.exe wrap
    if($LASTEXITCODE -ne 0) { throw 'Tick-wrap simulation failed.' }
} finally { Pop-Location }


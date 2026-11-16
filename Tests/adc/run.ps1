$ErrorActionPreference='Stop'
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $taskRoot
try {
 & 'C:/Program Files/CodeBlocks/MinGW/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror -DFW_CUSTOM_HMI=1 -DFW_FINAL_APPLICATION=1 -include Tests/adc/mock.h -ICore/Inc -ILibraries/Hmi/include Core/Src/HmiMonitorIo.c Libraries/Hmi/src/HmiDiagData.c Tests/adc/test.c -o Tests/adc/test.exe
 if($LASTEXITCODE -ne 0){throw 'ADC acquisition regression compile failed'}
 & ./Tests/adc/test.exe
 if($LASTEXITCODE -ne 0){throw 'ADC acquisition regression failed'}
} finally {Pop-Location}

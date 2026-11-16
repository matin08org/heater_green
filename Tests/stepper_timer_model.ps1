# Register-level timing model, not a substitute for oscilloscope verification.
# PWM2 rises at CCR3 and falls at update. Each update clocks TIM2.
# TIM2 OPM closes the TIM3 gate at ARR overflow, before CPU handles the IRQ.
$ErrorActionPreference = 'Stop'
function Assert-True($value, $message) {
    if (-not $value) { throw $message }
}
foreach ($distance in @(1, 2, 400, 800)) {
    $phase = 0
    $period = 10
    $compare = 5
    $counter = 1
    $rises = 0
    $falls = 0
    $gate = $true
    $high = $false
    $irq = $false
    # Keep CPU absent for many periods after the expected endpoint.
    for ($clock = 0; $clock -lt (($distance + 100) * $period); $clock++) {
        if (-not $gate) { continue }
        $phase++
        if ($phase -eq $compare) { $high = $true; $rises++ }
        if ($phase -eq $period) {
            $phase = 0
            $high = $false
            $falls++
            if ($counter -eq $distance) {
                $counter = 0
                $gate = $false # TIM2 OPM clears CEN / TRGO_ENABLE.
                $irq = $true
            } else { $counter++ }
        }
    }
    Assert-True ($rises -eq $distance -and $falls -eq $distance) "Pulse count failed: $distance"
    Assert-True ($irq -and -not $gate -and -not $high) 'Endpoint must freeze STEP LOW without CPU'
}
# Signed absolute positions must safely cover the full int32 range.
foreach ($pair in @(@(0,400), @(400,0), @(0,-400), @(-400,400), @(-2147483648,2147483647))) {
    $delta = [long]$pair[1] - [long]$pair[0]
    $distance = [Math]::Abs($delta)
    Assert-True ($distance -gt 0 -and $distance -le [uint32]::MaxValue) 'Distance overflow'
    $sign = if ($delta -gt 0) { 1L } else { -1L }
    Assert-True (([long]$pair[0] + $sign * $distance) -eq [long]$pair[1]) 'Absolute endpoint mismatch'
}
'PASS: 1/2/400/800 complete pulses, hardware gate despite delayed IRQ, signed endpoint ranges.'
'Hardware follow-up: measure PC8/PC9; delay TIM2 ISR and verify no extra STEP edges.'

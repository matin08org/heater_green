# Hardware-independent verification engine. Transport scriptblocks return objects:
# Read: @{ Success = bool; Data = byte[] }; Write/Reset: @{ Success = bool }.
function Test-FlashBytes {
    param([byte[]]$Expected, $ReadResult)
    if (-not $ReadResult.Success -or $null -eq $ReadResult.Data -or
        $ReadResult.Data.Length -ne $Expected.Length) { return $false }
    for ($i=0; $i -lt $Expected.Length; $i++) {
        if ($ReadResult.Data[$i] -ne $Expected[$i]) { return $false }
    }
    return $true
}

function Invoke-VerifiedFlashImage {
    param(
        [Parameter(Mandatory)][byte[]]$Image,
        [Parameter(Mandatory)][scriptblock]$Read,
        [Parameter(Mandatory)][scriptblock]$Write,
        [Parameter(Mandatory)][scriptblock]$Reset,
        [ValidateRange(1,5)][int]$MaxAttempts=3
    )
    if ($Image.Length -eq 0 -or $Image.Length -gt 2097152) { throw 'Image must fit the 2 MB external flash.' }
    $base=0x90000000L
    $blockSize=4096 # One erase sector. A retry cannot erase a neighbouring block.
    $written=0; $skipped=0; $attempts=0
    Write-Host 'Reading existing image twice to identify blocks already correct.'
    $initialA=& $Read $base $Image.Length
    $initialB=& $Read $base $Image.Length
    for ($offset=0; $offset -lt $Image.Length; $offset+=$blockSize) {
        $count=[Math]::Min($blockSize,$Image.Length-$offset)
        $block=New-Object byte[] $count
        [Array]::Copy($Image,$offset,$block,0,$count)
        $address=$base+$offset
        $matches=$initialA.Success -and $initialB.Success -and
                 $initialA.Data.Length -eq $Image.Length -and $initialB.Data.Length -eq $Image.Length
        if ($matches) {
            for ($i=0; $i -lt $count; $i++) {
                if ($initialA.Data[$offset+$i] -ne $block[$i] -or $initialB.Data[$offset+$i] -ne $block[$i]) {
                    $matches=$false; break
                }
            }
        }
        if ($matches) { $skipped++; continue }
        $verified=$false
        for ($try=1; $try -le $MaxAttempts; $try++) {
            # A mismatch in an initial large read may itself be a transient read
            # error. Confirm this small block before erasing it unnecessarily.
            $a=& $Read $address $count
            $b=& $Read $address $count
            if ((Test-FlashBytes $block $a) -and (Test-FlashBytes $block $b)) {
                $verified=$true; $skipped++; break
            }
            Write-Host ('Block 0x{0:X8}: write attempt {1}/{2}' -f $address,$try,$MaxAttempts)
            $attempts++
            $writeResult=& $Write $address $block
            # Never accept a download exit code alone. Both independent reads
            # must match even if the CLI's own verify reported a transient error.
            $a=& $Read $address $count
            $b=& $Read $address $count
            if ((Test-FlashBytes $block $a) -and (Test-FlashBytes $block $b)) {
                if (-not $writeResult.Success) {
                    Write-Host 'CLI reported a write/verify error; two independent block reads now match.'
                }
                $verified=$true; $written++; break
            }
        }
        if (-not $verified) {
            throw ('Persistent flash mismatch at block 0x{0:X8} after {1} attempts. Board NOT restarted. Check loader, QSPI signals, supply and wiring.' -f $address,$MaxAttempts)
        }
    }
    # Recheck the complete image. Require two consecutive exact reads before
    # allowing reset, including blocks skipped earlier. Read failures break the streak.
    $streak=0
    for ($try=1; $try -le 4; $try++) {
        Write-Host "Final complete readback $try/4; matching streak $streak/2."
        $full=& $Read $base $Image.Length
        if (Test-FlashBytes $Image $full) { $streak++ } else { $streak=0 }
        if ($streak -eq 2) { break }
    }
    if ($streak -lt 2) { throw 'Final image verification failed. Board NOT restarted.' }
    $resetResult=& $Reset
    if (-not $resetResult.Success) { throw 'Image verified, but reset failed.' }
    return [pscustomobject]@{ WrittenBlocks=$written; SkippedBlocks=$skipped; WriteAttempts=$attempts; Verified=$true }
}

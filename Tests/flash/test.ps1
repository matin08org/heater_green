$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../Flash-Verify.ps1')
function Assert-True($condition,$message) { if (-not $condition) { throw $message } }
function Run-Scenario([string]$name) {
    $script:scenario=$name
    $script:image=New-Object byte[] 8301
    for ($i=0;$i -lt $script:image.Length;$i++) { $script:image[$i]=($i*17+11)%256 }
    $script:device=New-Object byte[] $script:image.Length
    $script:writes=0; $script:resets=0; $script:fullReads=0; $script:readGlitch=$false
    if ($name -eq 'already-correct') { [Array]::Copy($script:image,$script:device,$script:image.Length) }
    if ($name -eq 'resume') { [Array]::Copy($script:image,$script:device,4096) }
    $read={
        param([long]$address,[int]$count)
        $offset=[int]($address-0x90000000L)
        $copy=New-Object byte[] $count
        [Array]::Copy($script:device,$offset,$copy,0,$count)
        if ($count -eq $script:image.Length) {
            $script:fullReads++
            if (($script:scenario -eq 'final-failure' -and $script:fullReads -gt 2) -or
                ($script:scenario -eq 'transient-final' -and $script:fullReads -eq 3)) { $copy[0]=$copy[0] -bxor 1 }
        }
        if ($script:scenario -eq 'transient-block' -and $count -le 4096 -and $script:writes -gt 0 -and -not $script:readGlitch) {
            $script:readGlitch=$true; $copy[0]=$copy[0] -bxor 1
        }
        return [pscustomobject]@{Success=$true;Data=$copy}
    }
    $write={
        param([long]$address,[byte[]]$data)
        $script:writes++
        $offset=[int]($address-0x90000000L)
        [Array]::Copy($data,0,$script:device,$offset,$data.Length)
        if ($script:scenario -eq 'persistent' -or ($script:scenario -eq 'retry-write' -and $script:writes -eq 1)) {
            $script:device[$offset]=$script:device[$offset] -bxor 1
        }
        return [pscustomobject]@{Success=($script:scenario -ne 'cli-verify-glitch')}
    }
    $reset={ $script:resets++; return [pscustomobject]@{Success=$true} }
    $caught=$false
    try { $result=Invoke-VerifiedFlashImage -Image $script:image -Read $read -Write $write -Reset $reset -MaxAttempts 3 6>$null }
    catch { $caught=$true; $message=$_.Exception.Message }
    if ($name -eq 'persistent' -or $name -eq 'final-failure') {
        Assert-True $caught "Expected a bounded failure: $name"
        Assert-True ($script:resets -eq 0) "Reset despite mismatch: $name"
        if ($name -eq 'persistent') { Assert-True ($script:writes -eq 3) 'Retry count exceeded its bound' }
    } else {
        Assert-True (-not $caught) "Unexpected failure ${name}: $message"
        Assert-True ($result.Verified -and $script:resets -eq 1) "Expected verified reset: $name"
        for ($i=0;$i -lt $script:image.Length;$i++) {
            Assert-True ($script:device[$i] -eq $script:image[$i]) 'Unverified data accepted'
        }
        if ($name -eq 'already-correct') { Assert-True ($script:writes -eq 0) 'Rewrote already verified flash' }
        if ($name -eq 'resume') { Assert-True ($result.SkippedBlocks -eq 1 -and $script:writes -eq 2) 'Resume did not skip the correct block' }
        if ($name -eq 'retry-write') { Assert-True ($script:writes -eq 4) 'Did not retry just the failed block' }
    }
    Write-Output "PASS: $name"
}
foreach ($scenarioName in @('normal','already-correct','resume','retry-write','transient-block','transient-final','cli-verify-glitch','persistent','final-failure')) {
    Run-Scenario $scenarioName
}
$tokens=$null; $errors=$null
[void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../../Program-Final.ps1'),[ref]$tokens,[ref]$errors)
Assert-True ($errors.Count -eq 0) 'Program-Final syntax error'
Write-Output 'PASS: Program-Final syntax; no hardware accessed'

$ErrorActionPreference = 'Stop'
$baseUri = 'http://localhost:8080/api'
try {
    $health = Invoke-RestMethod -Uri "$baseUri/health" -TimeoutSec 3
} catch {
    [Console]::Error.WriteLine('The debugger backend is unavailable at http://localhost:8080.')
    [Console]::Error.WriteLine('In a separate PowerShell window, from the project folder, run:')
    [Console]::Error.WriteLine('powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run.ps1')
    [Console]::Error.WriteLine('Keep that window open, then rerun demo.ps1 in this window.')
    exit 1
}
if ($health.engine -ne 'custom-ptrace') {
    [Console]::Error.WriteLine('Port 8080 is serving a different application. Start the debugger backend before running demo.ps1.')
    exit 1
}
function Control($sessionId, $action, $step = $null) {
    $body = @{ action = $action }
    if ($null -ne $step) { $body.step = $step }
    Invoke-RestMethod -Uri "$baseUri/sessions/$sessionId/control" -Method Post -ContentType 'application/json' -Body ($body | ConvertTo-Json -Compress)
}
$initial = Invoke-RestMethod -Uri "$baseUri/sessions" -Method Post -ContentType 'application/json' -Body '{"demo":"crash"}'
$sessionId = $initial.id
$null = Control $sessionId 'continue'
$deadline = [DateTime]::UtcNow.AddSeconds(15)
do {
    Start-Sleep -Milliseconds 50
    $fault = Invoke-RestMethod -Uri "$baseUri/sessions/$sessionId"
    if ([DateTime]::UtcNow -gt $deadline) { throw 'Crash demo timed out' }
} while ($fault.running)
if ($fault.status -ne 'crashed' -or $fault.live.signal -ne 11) { throw 'Expected SIGSEGV was not captured' }
$terminated = Control $sessionId 'step'
if ($terminated.status -ne 'terminated') { throw 'Original process did not terminate' }
$targets = @(1, 39, 57, ($fault.cursor - 1), $fault.cursor)
$proofs = @()
foreach ($target in $targets) {
    $history = Invoke-RestMethod -Uri "$baseUri/sessions/$sessionId/state?step=$target"
    if ($history.execution_restored) { throw 'History response incorrectly claimed restoration' }
    $restored = Control $sessionId 'restore' $target
    if (-not $restored.restoration.verified -or $restored.pid -le 0 -or $restored.pid -eq $initial.pid) { throw 'Live restoration was not verified' }
    foreach ($property in $history.event.registers.PSObject.Properties) {
        if ($restored.live.registers.($property.Name) -ne $property.Value) { throw "Register mismatch: $($property.Name)" }
    }
    if ($restored.live.memory.hex -ne $history.event.memory.hex) { throw 'Selected memory mismatch' }
    if ($restored.observed_output_hex.Length -ne 0) { throw 'Replay emitted duplicate child output' }
    $proofs += $restored.restoration
}
$final = Control $sessionId 'restore' 39
[PSCustomObject]@{
    result = 'PASS'
    original_pid = $initial.pid
    original_status = $terminated.status
    crash_step = $fault.cursor
    historical_execution_restored = $false
    verified_targets = $proofs
    current_session = $sessionId
    current_pid = $final.pid
    current_step = $final.cursor
    current_mode = $final.mode
    registers_and_selected_memory = 'matched at all targets'
    external_replay_output = 'zero bytes'
} | ConvertTo-Json -Depth 8

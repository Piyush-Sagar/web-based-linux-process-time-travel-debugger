$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
Write-Output 'Starting Linux backend. Open http://localhost:8080. Ctrl+C stops the server.'
& wsl -d Ubuntu --cd $projectRoot -- ./build/ttd-server 8080 traces
if ($LASTEXITCODE -ne 0) { throw 'Backend failed; run scripts/setup.ps1 first and check port 8080.' }

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
& wsl -d Ubuntu --cd $projectRoot -- cmake --build build -j 2
if ($LASTEXITCODE -ne 0) { throw 'Linux build failed' }
& wsl -d Ubuntu --cd $projectRoot -- ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Engine/API checks failed' }
Push-Location -LiteralPath (Join-Path $projectRoot 'frontend')
try {
    & npm.cmd run build
    if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed' }
    & npx.cmd playwright install chromium
    if ($LASTEXITCODE -ne 0) { throw 'Browser installation failed' }
    & npm.cmd run test:e2e
    if ($LASTEXITCODE -ne 0) { throw 'Browser tests failed' }
} finally { Pop-Location }

# SessionStart hook — PowerShell (Windows)
$root = if ($env:CLAUDE_PROJECT_DIR) { $env:CLAUDE_PROJECT_DIR } else { (Get-Location).Path }
Set-Location $root -ErrorAction SilentlyContinue
Write-Host "=== Project context ============================================="
Write-Host "  Dir  : $(Get-Location)"
Write-Host "  Date : $(Get-Date)"
$null = git rev-parse --git-dir 2>$null
if ($LASTEXITCODE -eq 0) {
  Write-Host "  Branch      : $(git branch --show-current 2>$null)"
  Write-Host "  Last commit : $(git log --oneline -1 2>$null)"
}
if (Test-Path "CMakeLists.txt") { Write-Host "  Stack : C++ / CMake" }
if (Test-Path "Cargo.toml")     { Write-Host "  Stack : Rust" }
if (Test-Path "go.mod")         { Write-Host "  Stack : Go" }
if (Test-Path "package.json")   { Write-Host "  Stack : Node.js / JS / TS" }
if (Test-Path "pyproject.toml") { Write-Host "  Stack : Python" }
if (Test-Path "pom.xml")        { Write-Host "  Stack : Java / Maven" }
if (Get-Item "*.sln" -ErrorAction SilentlyContinue) { Write-Host "  Stack : .NET" }
Write-Host "================================================================="
exit 0

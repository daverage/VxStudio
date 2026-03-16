# Stop hook — PowerShell. Shows git diff summary at end of each turn.
$root = if ($env:CLAUDE_PROJECT_DIR) { $env:CLAUDE_PROJECT_DIR } else { (Get-Location).Path }
Set-Location $root -ErrorAction SilentlyContinue
$null = git rev-parse --git-dir 2>$null
if ($LASTEXITCODE -eq 0) {
  $changed   = git diff --stat HEAD 2>$null | Select-Object -Last 1
  $untracked = (git ls-files --others --exclude-standard 2>$null | Measure-Object -Line).Lines
  if ($changed -or $untracked -gt 0) {
    Write-Host "`n=== Turn summary ================================================="
    if ($changed)         { Write-Host "  Modified : $changed" }
    if ($untracked -gt 0) { Write-Host "  New files: $untracked untracked" }
    Write-Host "================================================================="
  }
}
exit 0

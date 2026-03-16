# PostToolUseFailure — PowerShell. Logs failed tool calls.
$root = if ($env:CLAUDE_PROJECT_DIR) { $env:CLAUDE_PROJECT_DIR } else { (Get-Location).Path }
$log  = "$root\.claude\tool-failures.log"
$raw  = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch {}
$tool = if ($data.tool_name) { $data.tool_name } else { "unknown" }
$ts   = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null
"[$ts] tool=$tool" | Add-Content $log
$raw | Add-Content $log
"---" | Add-Content $log
exit 0

# PreToolUse:Bash — PowerShell. exit 2 = blocking error fed back to Claude.
$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }
$cmd = if ($data.tool_input.command) { $data.tool_input.command } else { "" }
$blocked = @("rm -rf /","rm -rf ~","format ","del /s /q c:\\","rd /s /q c:\\")
foreach ($p in $blocked) {
  if ($cmd -match [regex]::Escape($p)) {
    [Console]::Error.WriteLine("BLOCKED: Dangerous pattern: $p")
    [Console]::Error.WriteLine("Command: $cmd")
    exit 2
  }
}
if ($cmd -match "git push.*(--force|-f).*(main|master|production|prod|release)") {
  [Console]::Error.WriteLine("BLOCKED: Force-push to protected branch not allowed.")
  exit 2
}
exit 0

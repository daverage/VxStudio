# PreToolUse:Edit|Write — PowerShell. Blocks writes to secrets and lock files.
$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }
$file = if ($data.tool_input.path) { $data.tool_input.path } else { "" }
if (-not $file) { exit 0 }
$sensitive = @("\.env$","\.env\.","id_rsa","id_ed25519","\.pem$","\.key$","secrets\.","credentials\.")
foreach ($p in $sensitive) {
  if ($file -match $p) { [Console]::Error.WriteLine("BLOCKED: Sensitive file: $file"); exit 2 }
}
$locks = @("package-lock\.json$","yarn\.lock$","pnpm-lock\.yaml$","Cargo\.lock$","go\.sum$")
foreach ($p in $locks) {
  if ($file -match $p) { [Console]::Error.WriteLine("BLOCKED: Do not hand-edit lock file: $file"); exit 2 }
}
exit 0

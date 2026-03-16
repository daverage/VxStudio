# PostToolUse:Edit|Write — PowerShell. Runs build/lint after file save.
$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }
$file = if ($data.tool_input.path) { $data.tool_input.path } else { "" }
if (-not $file) { exit 0 }
$ext  = [IO.Path]::GetExtension($file).TrimStart(".").ToLower()
$root = if ($env:CLAUDE_PROJECT_DIR) { $env:CLAUDE_PROJECT_DIR } else { (Get-Location).Path }
$ncpu = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors

function Chk($label, $cmd) {
  $out = Invoke-Expression $cmd 2>&1
  if ($LASTEXITCODE -ne 0) {
    [Console]::Error.WriteLine("[$label] FAILED: $file")
    $out | Select-Object -Last 50 | ForEach-Object { [Console]::Error.WriteLine($_) }
    exit 1
  }
}

switch -Regex ($ext) {
  "^(cpp|cc|cxx|c|h|hpp|hxx)$" {
    if (Test-Path "$root\\build\\CMakeCache.txt") { Chk "CMake" "cmake --build '$root\\build' -j $ncpu" }
    elseif (Test-Path "$root\\Makefile")            { Chk "Make"  "make -C '$root'" }
  }
  "^(cs|vb|fs|fsx|csproj|vbproj|fsproj)$" {
    if (Get-Command dotnet -ErrorAction SilentlyContinue) {
      $t = Get-ChildItem $root -Recurse -Depth 3 -Include "*.sln","*.csproj","*.vbproj" | Select-Object -First 1
      if ($t) { Chk "dotnet" "dotnet build '$($t.FullName)' --no-restore -v quiet" }
    }
  }
  "^rs$"  { if ((Get-Command cargo -EA SilentlyContinue) -and (Test-Path "$root\\Cargo.toml"))  { Chk "cargo"   "cargo check --manifest-path '$root\\Cargo.toml' --quiet" } }
  "^go$"  { if ((Get-Command go    -EA SilentlyContinue) -and (Test-Path "$root\\go.mod"))      { Chk "go build" "go build $root\\..."; Chk "go vet" "go vet $root\\..." } }
  "^(ts|tsx|mts|cts)$" {
    if (Get-Command tsc -EA SilentlyContinue) {
      $tc = Get-ChildItem $root -Depth 2 -Filter "tsconfig*.json" | Select-Object -First 1
      if ($tc) { Chk "tsc" "tsc --project '$($tc.FullName)' --noEmit" }
      else     { Chk "tsc" "tsc --noEmit --strict '$file'" }
    }
  }
  "^(js|jsx|mjs|cjs)$" { if (Get-Command eslint -EA SilentlyContinue) { Chk "ESLint" "eslint '$file' --quiet" } }
  "^py$"  {
    if (Get-Command ruff   -EA SilentlyContinue) { Chk "ruff"   "ruff check '$file' --quiet" }
    elseif (Get-Command flake8 -EA SilentlyContinue) { Chk "flake8" "flake8 '$file'" }
  }
  "^php$" { if (Get-Command php -EA SilentlyContinue) { Chk "php -l" "php -l '$file'" } }
  "^java$" {
    if ((Get-Command mvn -EA SilentlyContinue) -and (Test-Path "$root\\pom.xml")) { Chk "Maven" "mvn compile -f '$root\\pom.xml' -q" }
    elseif (Get-Command gradle -EA SilentlyContinue) { Chk "Gradle" "gradle compileJava -p '$root' --quiet" }
  }
  "^(kt|kts)$" { if (Get-Command gradle -EA SilentlyContinue) { Chk "Gradle Kotlin" "gradle compileKotlin -p '$root' --quiet" } }
  "^swift$" {
    if ((Get-Command swift -EA SilentlyContinue) -and (Test-Path "$root\\Package.swift")) { Chk "swift" "swift build --package-path '$root'" }
  }
  "^rb$" {
    if (Get-Command ruby   -EA SilentlyContinue) { Chk "ruby -c" "ruby -c '$file'" }
    if (Get-Command rubocop -EA SilentlyContinue) { Chk "rubocop" "rubocop '$file' --no-color --format quiet" }
  }
  "^dart$" {
    if (Get-Command flutter -EA SilentlyContinue) { Chk "flutter" "flutter analyze '$file'" }
    elseif (Get-Command dart -EA SilentlyContinue) { Chk "dart" "dart analyze '$file'" }
  }
}
exit 0

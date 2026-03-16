#!/usr/bin/env bash
# PostToolUse:Edit|Write — runs build/lint for the saved file's language.
# exit 1 = check failed; stderr fed back to Claude as context.
INPUT=$(cat)
FILE=$(echo "$INPUT" | python3 -c \
  "import sys,json; d=json.load(sys.stdin); print(d.get('tool_input',{}).get('path',''))" 2>/dev/null)
[ -z "$FILE" ] && exit 0
EXT=$(echo "${FILE##*.}" | tr '[:upper:]' '[:lower:]')
ROOT="${CLAUDE_PROJECT_DIR:-$PWD}"
NCPU=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

chk() {
  local label="$1"; shift
  local out; out=$("$@" 2>&1); local s=$?
  [ $s -ne 0 ] && { echo "[$label] FAILED: $FILE" >&2; echo "$out" | tail -50 >&2; exit 1; }
}
chk_sh() {
  local label="$1"; shift
  local out; out=$(eval "$*" 2>&1); local s=$?
  [ $s -ne 0 ] && { echo "[$label] FAILED: $FILE" >&2; echo "$out" | tail -50 >&2; exit 1; }
}

case "$EXT" in
  c|cpp|cc|cxx|c++|h|hpp|hxx|hh|inl)
    if [ -d "$ROOT/build" ]; then
      if   [ -f "$ROOT/build/build.ninja" ] && command -v ninja >/dev/null 2>&1; then chk "Ninja" ninja -C "$ROOT/build" -j"$NCPU"
      elif [ -f "$ROOT/build/CMakeCache.txt" ] && command -v cmake >/dev/null 2>&1; then chk "CMake" cmake --build "$ROOT/build" -j"$NCPU"
      elif [ -f "$ROOT/Makefile" ] && command -v make >/dev/null 2>&1; then chk "Make" make -C "$ROOT" -j"$NCPU"
      fi
    fi
    command -v clang-tidy >/dev/null 2>&1 && [ -f "$ROOT/.clang-tidy" ] && chk_sh "clang-tidy" "clang-tidy '$FILE' --quiet 2>&1 | head -40"
    ;;
  cs|vb|fs|fsx|fsi|csproj|vbproj|fsproj)
    command -v dotnet >/dev/null 2>&1 && {
      T=$(find "$ROOT" -maxdepth 3 \( -name "*.sln" -o -name "*.*proj" \) | head -1)
      [ -n "$T" ] && chk_sh "dotnet" "dotnet build '$T' --no-restore -v quiet 2>&1 | tail -30"
    } ;;
  rs)
    command -v cargo >/dev/null 2>&1 && [ -f "$ROOT/Cargo.toml" ] &&
      chk_sh "cargo" "cargo check --manifest-path '$ROOT/Cargo.toml' --quiet 2>&1 | tail -30" ;;
  go)
    command -v go >/dev/null 2>&1 && [ -f "$ROOT/go.mod" ] &&
      { chk "go build" go build "$ROOT/..."; chk "go vet" go vet "$ROOT/..."; } ;;
  js|jsx|mjs|cjs)
    command -v eslint >/dev/null 2>&1 &&
      [ -n "$(find "$ROOT" -maxdepth 2 \( -name '.eslintrc*' -o -name 'eslint.config*' \) | head -1)" ] &&
      chk "ESLint" eslint "$FILE" --quiet ;;
  ts|tsx|mts|cts)
    command -v tsc >/dev/null 2>&1 && {
      T=$(find "$ROOT" -maxdepth 2 -name "tsconfig*.json" | head -1)
      if [ -n "$T" ]; then chk_sh "tsc" "tsc --project '$T' --noEmit 2>&1 | tail -30"
      else chk_sh "tsc" "tsc --noEmit --strict '$FILE' 2>&1 | tail -20"; fi
    } ;;
  py)
    if   command -v ruff   >/dev/null 2>&1; then chk "ruff"   ruff check "$FILE" --quiet
    elif command -v flake8 >/dev/null 2>&1; then chk "flake8" flake8 "$FILE" --max-line-length=120 --quiet
    elif command -v pylint >/dev/null 2>&1; then chk_sh "pylint" "pylint '$FILE' --errors-only --score=no 2>&1 | tail -20"
    fi
    command -v mypy >/dev/null 2>&1 && {
      for f in mypy.ini setup.cfg pyproject.toml .mypy.ini; do
        [ -f "$ROOT/$f" ] && chk_sh "mypy" "mypy '$FILE' --ignore-missing-imports --no-error-summary 2>&1 | tail -20" && break
      done
    } ;;
  php)
    command -v php    >/dev/null 2>&1 && chk "php -l" php -l "$FILE"
    command -v phpstan >/dev/null 2>&1 && chk_sh "phpstan" "phpstan analyse '$FILE' --level=5 --no-progress 2>&1 | tail -20" ;;
  java)
    if   command -v mvn    >/dev/null 2>&1 && [ -f "$ROOT/pom.xml" ]; then chk_sh "Maven"  "mvn compile -f '$ROOT/pom.xml' -q 2>&1 | tail -30"
    elif command -v gradle >/dev/null 2>&1 && { [ -f "$ROOT/build.gradle" ] || [ -f "$ROOT/build.gradle.kts" ]; }; then chk_sh "Gradle" "gradle compileJava -p '$ROOT' --quiet 2>&1 | tail -30"
    elif command -v javac  >/dev/null 2>&1; then chk_sh "javac" "javac '$FILE' -d /tmp 2>&1 | tail -20"; fi ;;
  kt|kts)
    if   command -v gradle  >/dev/null 2>&1 && { [ -f "$ROOT/build.gradle.kts" ] || [ -f "$ROOT/build.gradle" ]; }; then chk_sh "Gradle Kotlin" "gradle compileKotlin -p '$ROOT' --quiet 2>&1 | tail -30"
    elif command -v kotlinc >/dev/null 2>&1; then chk_sh "kotlinc" "kotlinc '$FILE' -include-runtime -d /tmp/out.jar 2>&1 | tail -20"; fi ;;
  swift)
    if   command -v swift  >/dev/null 2>&1 && [ -f "$ROOT/Package.swift" ]; then chk_sh "swift build" "swift build --package-path '$ROOT' 2>&1 | tail -30"
    elif command -v swiftc >/dev/null 2>&1; then chk_sh "swiftc" "swiftc -typecheck '$FILE' 2>&1 | tail -20"; fi ;;
  rb)
    command -v ruby    >/dev/null 2>&1 && chk "ruby -c" ruby -c "$FILE"
    command -v rubocop >/dev/null 2>&1 && chk_sh "rubocop" "rubocop '$FILE' --no-color --format quiet 2>&1 | tail -20" ;;
  scala|sc)
    command -v sbt >/dev/null 2>&1 && [ -f "$ROOT/build.sbt" ] && chk_sh "sbt" "sbt compile 2>&1 | tail -30" ;;
  hs|lhs)
    if   command -v cabal >/dev/null 2>&1 && ls "$ROOT"/*.cabal >/dev/null 2>&1; then chk_sh "cabal" "cabal build 2>&1 | tail -30"
    elif command -v stack >/dev/null 2>&1 && [ -f "$ROOT/stack.yaml" ]; then chk_sh "stack" "stack build 2>&1 | tail -30"; fi ;;
  lua)  command -v luac >/dev/null 2>&1 && chk "luac" luac -p "$FILE" ;;
  zig)  command -v zig  >/dev/null 2>&1 && [ -f "$ROOT/build.zig" ] && chk_sh "zig" "zig build 2>&1 | tail -30" ;;
  ex|exs)
    command -v mix >/dev/null 2>&1 && [ -f "$ROOT/mix.exs" ] &&
      chk_sh "mix" "cd '$ROOT' && mix compile --warnings-as-errors 2>&1 | tail -30" ;;
  dart)
    if   command -v flutter >/dev/null 2>&1 && [ -f "$ROOT/pubspec.yaml" ]; then chk_sh "flutter" "flutter analyze '$FILE' 2>&1 | tail -20"
    elif command -v dart    >/dev/null 2>&1; then chk_sh "dart" "dart analyze '$FILE' 2>&1 | tail -20"; fi ;;
esac
exit 0

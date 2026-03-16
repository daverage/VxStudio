#!/usr/bin/env bash
# SessionStart hook — shows project context when a Claude Code session opens.
cd "${CLAUDE_PROJECT_DIR:-$PWD}" 2>/dev/null || exit 0
echo "=== Project context ============================================="
echo "  Dir  : $PWD"
echo "  Date : $(date)"
if git rev-parse --git-dir >/dev/null 2>&1; then
  echo "  Branch      : $(git branch --show-current 2>/dev/null)"
  echo "  Last commit : $(git log --oneline -1 2>/dev/null)"
  DIRTY=$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')
  [ "$DIRTY" -gt 0 ] && echo "  Uncommitted : $DIRTY changed file(s)"
fi
[ -f CMakeLists.txt ]   && echo "  Stack : C++ / CMake"
[ -f Cargo.toml ]       && echo "  Stack : Rust"
[ -f go.mod ]           && echo "  Stack : Go"
[ -f package.json ]     && echo "  Stack : Node.js / JS / TS"
[ -f pyproject.toml ]   && echo "  Stack : Python"
[ -f composer.json ]    && echo "  Stack : PHP"
[ -f pom.xml ]          && echo "  Stack : Java / Maven"
[ -f build.gradle ]     && echo "  Stack : Java|Kotlin / Gradle"
[ -f Package.swift ]    && echo "  Stack : Swift / SPM"
[ -f mix.exs ]          && echo "  Stack : Elixir"
[ -f pubspec.yaml ]     && echo "  Stack : Dart / Flutter"
[ -f build.zig ]        && echo "  Stack : Zig"
for f in *.sln *.csproj *.vbproj; do [ -f "$f" ] && echo "  Stack : .NET" && break; done
echo "================================================================="
exit 0

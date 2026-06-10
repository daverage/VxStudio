#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  tools/release.sh v0.1.0 [options]

Create a release tag and push it to GitHub.
The GitHub Actions release workflow then builds macOS and Windows artifacts,
packages them, and publishes the GitHub Release.

Options:
  --message TEXT    Tag annotation message (default: VX Studio v0.1.0)
  --no-push         Create the tag locally only
  --remote NAME     Git remote to push to (default: origin)
  --help            Show this message

Examples:
  tools/release.sh v0.1.0
  tools/release.sh v0.1.0 --message "VX Studio v0.1.0"
EOF
}

die() {
  printf 'release: %s\n' "$*" >&2
  exit 1
}

info() {
  printf 'release: %s\n' "$*"
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

if [[ $# -eq 0 ]]; then
  usage
  exit 1
fi

if ! git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  die "not inside a git repository"
fi

require_cmd git

version=""
remote="origin"
message=""
push_tag=1

if [[ "${1:-}" != --* ]]; then
  version="${1#v}"
  shift
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --message)
      message="${2:?missing value for --message}"
      shift 2
      ;;
    --no-push)
      push_tag=0
      shift
      ;;
    --remote)
      remote="${2:?missing value for --remote}"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ -n "$version" ]] || die "you must supply a version, for example: tools/release.sh v0.1.0"
version="${version#v}"
tag="v${version}"

if [[ -z "$message" ]]; then
  message="VX Studio ${tag}"
fi

current_status="$(git -C "$repo_root" status --short)"
if [[ -n "$current_status" ]]; then
  info "working tree is not clean"
  printf '%s\n' "$current_status"
  die "commit or stash your changes before creating a release tag"
fi

if git -C "$repo_root" rev-parse "$tag" >/dev/null 2>&1; then
  die "tag already exists locally: ${tag}"
fi

if git -C "$repo_root" ls-remote --exit-code --tags "$remote" "refs/tags/$tag" >/dev/null 2>&1; then
  die "tag already exists on ${remote}: ${tag}"
fi

info "creating annotated tag ${tag}"
git -C "$repo_root" tag -a "$tag" -m "$message"

if [[ "$push_tag" -eq 1 ]]; then
  info "pushing tag to ${remote}"
  git -C "$repo_root" push "$remote" "$tag"
  info "GitHub Actions should now build and publish the release"
else
  info "tag created locally only; push it later with:"
  info "git push ${remote} ${tag}"
fi

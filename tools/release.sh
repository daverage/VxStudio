#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  tools/release.sh v1.0.1 [options]

Builds the current macOS release, packages the staged VST3 bundles, updates
the web downloads page, and optionally publishes GitHub Release assets.
If you already have a Windows ZIP from a separate build job, you can attach it
with --windows-zip and the script will publish it alongside the macOS asset.

Options:
  --build-dir DIR       CMake build directory (default: build)
  --stage-dir DIR       Staged plugin directory (default: Source/vxstudio/vst)
  --dist-dir DIR        Release output directory (default: dist/releases)
  --windows-zip FILE    Prebuilt Windows ZIP to publish alongside macOS
  --notes-file FILE     Release notes markdown file to upload/use
  --skip-build          Do not run cmake --build
  --skip-sign           Do not run signing/notarization helper
  --skip-web-sync       Do not copy artifacts or update the downloads page
  --publish             Create or update the GitHub release
  --draft               Mark the GitHub release as a draft
  --prerelease          Mark the GitHub release as a prerelease
  --help                Show this message

Environment:
  APPLE_DEVELOPER_IDENTITY  Enables signing in tools/release/sign_and_notarize_vst3.sh
  APPLE_NOTARY_PROFILE      Enables notarization in tools/release/sign_and_notarize_vst3.sh
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

human_size() {
  local bytes="$1"

  if (( bytes >= 1024 * 1024 * 1024 )); then
    printf '%d.%d GB' "$((bytes / 1073741824))" "$(((bytes % 1073741824) * 10 / 1073741824))"
  elif (( bytes >= 1024 * 1024 )); then
    printf '%d.%d MB' "$((bytes / 1048576))" "$(((bytes % 1048576) * 10 / 1048576))"
  elif (( bytes >= 1024 )); then
    printf '%d.%d KB' "$((bytes / 1024))" "$(((bytes % 1024) * 10 / 1024))"
  else
    printf '%d B' "$bytes"
  fi
}

sha256_file() {
  local path="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
  else
    die "need shasum or sha256sum to compute checksums"
  fi
}

replace_release_placeholders() {
  local file="$1"
  local release_version="$2"
  local release_size="$3"
  local release_checksum="$4"
  local macos_download_url="$5"
  local release_page_url="$6"
  local checksums_url="$7"
  local windows_download_url="$8"
  local windows_status_class="$9"
  local windows_status_text="${10}"
  local windows_version_text="${11}"
  local windows_file_size_text="${12}"
  local windows_checksum_text="${13}"
  local windows_live_style="${14}"
  local windows_soon_style="${15}"

  VXSTUDIO_RELEASE_VERSION="$release_version" \
  VXSTUDIO_RELEASE_SIZE="$release_size" \
  VXSTUDIO_RELEASE_CHECKSUM="$release_checksum" \
  VXSTUDIO_MACOS_DOWNLOAD_URL="$macos_download_url" \
  VXSTUDIO_RELEASE_PAGE_URL="$release_page_url" \
  VXSTUDIO_CHECKSUMS_URL="$checksums_url" \
  VXSTUDIO_WINDOWS_DOWNLOAD_URL="$windows_download_url" \
  VXSTUDIO_WINDOWS_STATUS_CLASS="$windows_status_class" \
  VXSTUDIO_WINDOWS_STATUS_TEXT="$windows_status_text" \
  VXSTUDIO_WINDOWS_VERSION_TEXT="$windows_version_text" \
  VXSTUDIO_WINDOWS_FILE_SIZE_TEXT="$windows_file_size_text" \
  VXSTUDIO_WINDOWS_CHECKSUM_TEXT="$windows_checksum_text" \
  VXSTUDIO_WINDOWS_LIVE_STYLE="$windows_live_style" \
  VXSTUDIO_WINDOWS_SOON_STYLE="$windows_soon_style" \
  perl -0pi -e '
    my $version  = $ENV{VXSTUDIO_RELEASE_VERSION};
    my $size     = $ENV{VXSTUDIO_RELEASE_SIZE};
    my $checksum = $ENV{VXSTUDIO_RELEASE_CHECKSUM};
    my $macos    = $ENV{VXSTUDIO_MACOS_DOWNLOAD_URL};
    my $release  = $ENV{VXSTUDIO_RELEASE_PAGE_URL};
    my $checks   = $ENV{VXSTUDIO_CHECKSUMS_URL};
    my $win_url  = $ENV{VXSTUDIO_WINDOWS_DOWNLOAD_URL};
    my $win_cls  = $ENV{VXSTUDIO_WINDOWS_STATUS_CLASS};
    my $win_txt  = $ENV{VXSTUDIO_WINDOWS_STATUS_TEXT};
    my $win_ver  = $ENV{VXSTUDIO_WINDOWS_VERSION_TEXT};
    my $win_size = $ENV{VXSTUDIO_WINDOWS_FILE_SIZE_TEXT};
    my $win_sha  = $ENV{VXSTUDIO_WINDOWS_CHECKSUM_TEXT};
    my $win_live = $ENV{VXSTUDIO_WINDOWS_LIVE_STYLE};
    my $win_soon = $ENV{VXSTUDIO_WINDOWS_SOON_STYLE};
    s/\{\{RELEASE_VERSION\}\}/$version/g;
    s/\{\{RELEASE_SIZE\}\}/$size/g;
    s/\{\{RELEASE_CHECKSUM\}\}/$checksum/g;
    s/\{\{MACOS_DOWNLOAD_URL\}\}/$macos/g;
    s/\{\{RELEASE_PAGE_URL\}\}/$release/g;
    s/\{\{CHECKSUMS_URL\}\}/$checks/g;
    s/\{\{WINDOWS_DOWNLOAD_URL\}\}/$win_url/g;
    s/\{\{WINDOWS_STATUS_CLASS\}\}/$win_cls/g;
    s/\{\{WINDOWS_STATUS_TEXT\}\}/$win_txt/g;
    s/\{\{WINDOWS_VERSION_TEXT\}\}/$win_ver/g;
    s/\{\{WINDOWS_FILE_SIZE_TEXT\}\}/$win_size/g;
    s/\{\{WINDOWS_CHECKSUM_TEXT\}\}/$win_sha/g;
    s/\{\{WINDOWS_LIVE_STYLE\}\}/$win_live/g;
    s/\{\{WINDOWS_COMING_SOON_STYLE\}\}/$win_soon/g;
  ' "$file"
}

resolve_path() {
  local value="$1"
  if [[ "$value" = /* ]]; then
    printf '%s\n' "$value"
  else
    printf '%s\n' "$repo_root/$value"
  fi
}

make_release_notes() {
  local notes_path="$1"
  local version="$2"
  local tag="$3"
  local include_windows="$4"

  {
    printf '# VX Studio %s\n\n' "$version"
    printf 'Release tag: `%s`\n\n' "$tag"
    printf 'Artifacts:\n'
    printf -- '- `VXStudio-%s-macos.zip`\n' "$version"
    if [[ "$include_windows" -eq 1 ]]; then
      printf -- '- `VXStudio-%s-windows.zip`\n' "$version"
    fi
    printf -- '- `vxstudio-complete.zip`\n'
    printf -- '- `checksums.txt`\n\n'
    printf 'Build notes:\n'
    printf -- '- macOS VST3 bundles staged from `Source/vxstudio/vst/`\n'
    if [[ "$include_windows" -eq 1 ]]; then
      printf -- '- Windows ZIP supplied separately and uploaded with this release\n'
    fi
    printf -- '- Signed/notarized when `APPLE_DEVELOPER_IDENTITY` and `APPLE_NOTARY_PROFILE` are set\n'
    printf '\n'

    local previous_tag=""
    if previous_tag="$(git -C "$repo_root" describe --tags --abbrev=0 2>/dev/null || true)" && [[ -n "$previous_tag" ]]; then
      printf 'Changes since `%s`:\n' "$previous_tag"
      git -C "$repo_root" log --pretty=format:'- %s' "${previous_tag}..HEAD"
    else
      printf 'Recent commits:\n'
      git -C "$repo_root" log --pretty=format:'- %s' --max-count=12
    fi
    printf '\n'
  } > "$notes_path"
}

version=""
build_dir="${BUILD_DIR:-build}"
stage_dir="${STAGE_DIR:-Source/vxstudio/vst}"
dist_dir="${DIST_DIR:-dist/releases}"
notes_file=""
windows_zip_input=""
skip_build=0
skip_sign=0
skip_web_sync=0
publish=0
draft=0
prerelease=0

if [[ $# -eq 0 ]]; then
  usage
  exit 1
fi

if [[ "${1:-}" != --* ]]; then
  version="${1#v}"
  shift
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      build_dir="${2:?missing value for --build-dir}"
      shift 2
      ;;
    --stage-dir)
      stage_dir="${2:?missing value for --stage-dir}"
      shift 2
      ;;
    --dist-dir)
      dist_dir="${2:?missing value for --dist-dir}"
      shift 2
      ;;
    --notes-file)
      notes_file="${2:?missing value for --notes-file}"
      shift 2
      ;;
    --windows-zip)
      windows_zip_input="${2:?missing value for --windows-zip}"
      shift 2
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --skip-sign)
      skip_sign=1
      shift
      ;;
    --skip-web-sync)
      skip_web_sync=1
      shift
      ;;
    --publish)
      publish=1
      shift
      ;;
    --draft)
      draft=1
      shift
      ;;
    --prerelease)
      prerelease=1
      shift
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

build_path="$(resolve_path "$build_dir")"
stage_path="$(resolve_path "$stage_dir")"
dist_path="$(resolve_path "$dist_dir")"
windows_zip_source=""
if [[ -n "$windows_zip_input" ]]; then
  windows_zip_source="$(resolve_path "$windows_zip_input")"
  [[ -f "$windows_zip_source" ]] || die "windows zip not found: ${windows_zip_input}"
fi

[[ -n "$version" ]] || die "you must supply a version, for example: tools/release.sh v1.0.1"
version="${version#v}"
tag="v${version}"
release_title="VX Studio ${tag}"
repo_slug="$(git -C "$repo_root" remote get-url origin | sed -E 's#^(git@github.com:|https://github.com/)##; s#\.git$##')"
release_asset_base="https://github.com/${repo_slug}/releases/download/${tag}"
release_page_url="https://github.com/${repo_slug}/releases/tag/${tag}"
release_root="${dist_path}/${tag}"
package_root="${release_root}/package/VXStudio-${version}-macos"
versioned_zip="${release_root}/VXStudio-${version}-macos.zip"
windows_release_zip="${release_root}/VXStudio-${version}-windows.zip"
stable_zip="${release_root}/vxstudio-complete.zip"
checksums_path="${release_root}/checksums.txt"
notes_path="${release_root}/release-notes.md"
web_downloads_dir="${repo_root}/web/downloads"
web_index="${web_downloads_dir}/index.html"
web_zip="${web_downloads_dir}/vxstudio-complete.zip"
web_checksums="${web_downloads_dir}/checksums.txt"
windows_checksum_short=""
windows_download_url=""
windows_status_class="status-coming"
windows_status_text="Coming Soon"
windows_version_text="VX Studio for Windows (In Development)"
windows_file_size_text="📦 ~480 MB (estimated)"
windows_live_style="display: none;"
windows_soon_style="display: block;"

require_cmd find
require_cmd zip
require_cmd perl
require_cmd gh

if [[ ! -d "$build_path" && "${skip_build}" -eq 0 ]]; then
  die "build directory not found: ${build_dir}"
fi

mkdir -p "$release_root" "$package_root"

if [[ "$skip_build" -eq 0 ]]; then
  info "building ${build_dir}"
  (cd "$repo_root" && cmake --build "$build_path" --parallel)
fi

if [[ "$skip_sign" -eq 0 ]]; then
  info "running release preflight"
  "$repo_root/tools/release/release_preflight.sh" "$stage_path"

  info "signing/notarizing staged bundles when configured"
  "$repo_root/tools/release/sign_and_notarize_vst3.sh" "$stage_path"
fi

info "collecting staged bundles"
staged_bundles=()
while IFS= read -r bundle; do
  [[ -n "$bundle" ]] || continue
  staged_bundles+=("$bundle")
done < <(find "$stage_path" -maxdepth 1 -type d -name '*.vst3' | sort)
[[ ${#staged_bundles[@]} -gt 0 ]] || die "no staged bundles found in ${stage_dir}"

rm -rf "$package_root"
mkdir -p "$package_root"
for bundle in "${staged_bundles[@]}"; do
  info "staging $(basename "$bundle")"
  cp -R "$bundle" "$package_root/"
done

cat > "${package_root}/INSTALL.txt" <<EOF
VX Studio ${version}

macOS install
1. Copy the .vst3 bundles in this archive into /Library/Audio/Plug-Ins/VST3
   for all users, or ~/Library/Audio/Plug-Ins/VST3 for the current user.
2. Restart or rescan your DAW.

Release assets
- GitHub release ZIP: ${release_title}
- Website download: vxstudio-complete.zip
EOF

cat > "${package_root}/README.txt" <<EOF
VX Studio ${version}

This archive contains the macOS VST3 bundles staged from Source/vxstudio/vst.
See the GitHub release notes for the changelog and checksums.
EOF

info "creating zip archive"
rm -f "$versioned_zip" "$stable_zip"
if command -v ditto >/dev/null 2>&1; then
  (cd "$release_root" && ditto -c -k --keepParent "package/$(basename "$package_root")" "$(basename "$versioned_zip")")
else
  (cd "$release_root" && zip -qry "$(basename "$versioned_zip")" "package/$(basename "$package_root")")
fi
cp "$versioned_zip" "$stable_zip"

checksum="$(sha256_file "$stable_zip")"
size_bytes="$(stat -f%z "$stable_zip" 2>/dev/null || stat -c%s "$stable_zip")"
size_human="$(human_size "$size_bytes")"
checksum_short="${checksum:0:8}...${checksum: -8}"

printf '%s  %s\n' "$checksum" "vxstudio-complete.zip" > "$checksums_path"
printf '%s  %s\n' "$checksum" "$(basename "$versioned_zip")" >> "$checksums_path"

include_windows=0
if [[ -n "$windows_zip_source" ]]; then
  include_windows=1
  info "including windows zip"
  cp "$windows_zip_source" "$windows_release_zip"
  windows_checksum="$(sha256_file "$windows_release_zip")"
  windows_size_bytes="$(stat -f%z "$windows_release_zip" 2>/dev/null || stat -c%s "$windows_release_zip")"
  windows_size_human="$(human_size "$windows_size_bytes")"
  windows_checksum_short="${windows_checksum:0:8}...${windows_checksum: -8}"
  windows_download_url="${release_asset_base}/$(basename "$windows_release_zip")"
  windows_status_class="status-available"
  windows_status_text="Available"
  windows_version_text="VX Studio v${version} (Current Verified Build)"
  windows_file_size_text="📦 ${windows_size_human}"
  windows_live_style="display: block;"
  windows_soon_style="display: none;"
  printf '%s  %s\n' "$windows_checksum" "$(basename "$windows_release_zip")" >> "$checksums_path"
fi

if [[ "$skip_web_sync" -eq 0 ]]; then
  info "syncing web downloads"
  cp "$stable_zip" "$web_zip"
  cp "$checksums_path" "$web_checksums"
  replace_release_placeholders "$web_index" "v${version}" "$size_human" "$checksum_short" \
    "${release_asset_base}/vxstudio-complete.zip" \
    "$release_page_url" \
    "${release_asset_base}/checksums.txt" \
    "$windows_download_url" \
    "$windows_status_class" \
    "$windows_status_text" \
    "$windows_version_text" \
    "$windows_file_size_text" \
    "$windows_checksum_short" \
    "$windows_live_style" \
    "$windows_soon_style"
fi

make_release_notes "$notes_path" "$version" "$tag" "$include_windows"

if [[ "$publish" -eq 1 ]]; then
  info "publishing GitHub release ${tag}"
  if gh release view "$tag" >/dev/null 2>&1; then
    edit_args=("$tag" --title "$release_title" --notes-file "$notes_path")
    if [[ "$draft" -eq 1 ]]; then
      edit_args+=(--draft)
    fi
    if [[ "$prerelease" -eq 1 ]]; then
      edit_args+=(--prerelease)
    fi
    gh release edit "${edit_args[@]}"
    upload_args=(
      "$versioned_zip#VXStudio-${version}-macos.zip"
      "$stable_zip#vxstudio-complete.zip"
      "$checksums_path#checksums.txt"
    )
    if [[ -n "$windows_zip_source" ]]; then
      upload_args+=("$windows_release_zip#VXStudio-${version}-windows.zip")
    fi
    gh release upload "$tag" "${upload_args[@]}" --clobber
  else
    release_args=(
      "$tag"
      "$versioned_zip#VXStudio-${version}-macos.zip"
      "$stable_zip#vxstudio-complete.zip"
      "$checksums_path#checksums.txt"
    )
    if [[ -n "$windows_zip_source" ]]; then
      release_args+=("$windows_release_zip#VXStudio-${version}-windows.zip")
    fi
    release_args+=(--title "$release_title" --notes-file "$notes_path")
    if [[ "$draft" -eq 1 ]]; then
      release_args+=(--draft)
    fi
    if [[ "$prerelease" -eq 1 ]]; then
      release_args+=(--prerelease)
    fi
    gh release create "${release_args[@]}"
  fi
fi

info "done"
info "Versioned ZIP: ${versioned_zip}"
if [[ -n "$windows_zip_source" ]]; then
  info "Windows ZIP:   ${windows_release_zip}"
fi
info "Stable ZIP:    ${stable_zip}"
info "Checksums:     ${checksums_path}"
info "Notes:         ${notes_path}"
if [[ "$skip_web_sync" -eq 0 ]]; then
  info "Web ZIP:       ${web_zip}"
  info "Web checksums: ${web_checksums}"
fi

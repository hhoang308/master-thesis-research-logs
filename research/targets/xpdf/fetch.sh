#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE' >&2
usage: fetch.sh [xpdf] <version> [--force]

Downloads the locked xpdf source tarball, verifies SHA256, extracts it into:
  research/targets/xpdf/<version>/source

The tarball cache lives in:
  research/targets/xpdf/distfiles
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target="xpdf"
force=0

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

if [[ "${1:-}" == "xpdf" ]]; then
  shift
fi

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

version="$1"
shift

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) force=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
  esac
  shift
done

lock="$script_dir/sources.lock"
line="$(awk -F'|' -v t="$target" -v v="$version" '($1 == t && $2 == v) { print; found=1 } END { if (!found) exit 1 }' "$lock")" || {
  echo "no locked source entry for $target $version in $lock" >&2
  exit 1
}

IFS='|' read -r _ locked_version url filename sha256 archive_root <<<"$line"
dest_dir="$script_dir/$locked_version"
source_dir="$dest_dir/source"
dist_dir="$script_dir/distfiles"
archive="$dist_dir/$filename"

mkdir -p "$dist_dir" "$dest_dir"

if [[ -d "$source_dir" && "$force" -ne 1 ]]; then
  echo "source already exists: $source_dir"
  echo "use --force to delete and re-extract it"
  exit 0
fi

if [[ ! -f "$archive" ]]; then
  echo "downloading $url"
  if command -v curl >/dev/null 2>&1; then
    curl -fL "$url" -o "$archive"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$archive" "$url"
  else
    echo "curl or wget is required to download $url" >&2
    exit 1
  fi
fi

echo "$sha256  $archive" | sha256sum -c -

tmp_extract="$(mktemp -d "${TMPDIR:-/tmp}/xpdf-fetch.XXXXXX")"
cleanup() {
  rm -rf "$tmp_extract"
}
trap cleanup EXIT

tar -xzf "$archive" -C "$tmp_extract"

if [[ ! -d "$tmp_extract/$archive_root" ]]; then
  echo "archive root '$archive_root' was not found in $archive" >&2
  exit 1
fi

rm -rf "$source_dir"
mv "$tmp_extract/$archive_root" "$source_dir"

patch_file="$script_dir/patches/$locked_version-disable-required-qt.patch"
if [[ -f "$patch_file" ]]; then
  echo "applying patch: $patch_file"
  patch -d "$source_dir" -p0 < "$patch_file"
fi

echo "ready: $source_dir"

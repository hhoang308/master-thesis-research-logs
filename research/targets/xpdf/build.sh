#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE' >&2
usage: build.sh [xpdf] <version> <profile> [--target <cmake-target>] [--fresh]

Profiles:
  asan     Debug build with ASan/UBSan, for PoC reproduction.
  cov      Debug build with LLVM source-based coverage flags.
  afl      AFL++ instrumented build using afl-clang-fast/afl-clang-fast++.
  release  Plain Release build.

Build output:
  research/targets/xpdf/<version>/build-<profile>
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
target_name="xpdf"

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

if [[ "${1:-}" == "xpdf" ]]; then
  shift
fi

if [[ $# -lt 2 ]]; then
  usage
  exit 2
fi

version="$1"
profile="$2"
shift 2

cmake_target=""
fresh=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      [[ $# -ge 2 ]] || { echo "--target requires a value" >&2; exit 2; }
      cmake_target="$2"
      shift
      ;;
    --fresh)
      fresh=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
  shift
done

case "$profile" in
  asan|cov|afl|release) ;;
  *) echo "unknown profile: $profile" >&2; usage; exit 2 ;;
esac

source_dir="$script_dir/$version/source"
build_dir="$script_dir/$version/build-$profile"

if [[ ! -d "$source_dir" ]]; then
  "$script_dir/fetch.sh" "$target_name" "$version"
fi

if [[ "$fresh" -eq 1 ]]; then
  rm -rf "$build_dir"
fi
mkdir -p "$build_dir"

common_args=(
  -S "$source_dir"
  -B "$build_dir"
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt4=1
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt5Widgets=1
)

case "$profile" in
  asan)
    cmake_args=(
      "${common_args[@]}"
      -DCMAKE_BUILD_TYPE=Debug
      "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer"
    )
    ;;
  cov)
    cmake_args=(
      "${common_args[@]}"
      -DCMAKE_BUILD_TYPE=Debug
      "-DCMAKE_CXX_FLAGS=-O1 -g -fprofile-instr-generate -fcoverage-mapping"
      "-DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate -fcoverage-mapping"
    )
    ;;
  afl)
    afl_cc="${AFL_CC:-$(command -v afl-clang-fast || true)}"
    afl_cxx="${AFL_CXX:-$(command -v afl-clang-fast++ || true)}"
    if [[ -z "$afl_cc" || -z "$afl_cxx" ]]; then
      echo "AFL++ compilers not found; set AFL_CC and AFL_CXX or install AFL++" >&2
      exit 1
    fi
    cmake_args=(
      "${common_args[@]}"
      -DCMAKE_BUILD_TYPE=Debug
      -DCMAKE_C_COMPILER="$afl_cc"
      -DCMAKE_CXX_COMPILER="$afl_cxx"
      "-DCMAKE_CXX_FLAGS=-O1 -g"
    )
    ;;
  release)
    cmake_args=(
      "${common_args[@]}"
      -DCMAKE_BUILD_TYPE=Release
    )
    ;;
esac

cmake "${cmake_args[@]}"

if [[ -n "$cmake_target" ]]; then
  cmake --build "$build_dir" --target "$cmake_target"
else
  cmake --build "$build_dir"
fi

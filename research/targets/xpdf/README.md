# xpdf Targets

This directory stores reproducible build metadata for xpdf target versions.
Downloaded tarballs, extracted source trees, and build directories are local
artifacts and are ignored by Git.

## Layout

```text
research/targets/xpdf/
  sources.lock
  fetch.sh
  build.sh
  patches/
  4.02/source/      # generated, ignored
  4.02/build-asan/  # generated, ignored
  4.03/source/      # generated, ignored
  4.06/source/      # generated, ignored
```

## Fetch Sources

```sh
cd /home/parkle/master-thesis-research-logs/research/targets/xpdf
./fetch.sh xpdf 4.02
./fetch.sh xpdf 4.03
./fetch.sh xpdf 4.06
```

`fetch.sh` downloads the tarball listed in `sources.lock`, verifies SHA256,
extracts it into `<version>/source`, and applies tracked patches when needed.

## Build

```sh
./build.sh xpdf 4.02 asan --target pdftops
./build.sh xpdf 4.03 asan --target pdftops
./build.sh xpdf 4.06 afl --target pdftotext
```

Build outputs live under `<version>/build-<profile>` and are ignored. Supported
profiles are `asan`, `cov`, `afl`, and `release`.

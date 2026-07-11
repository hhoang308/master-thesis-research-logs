# Targets

Store target metadata and local builds as `targets/<target>/<version>/`.

Target source trees, downloaded tarballs, and build directories are local
artifacts and are ignored. Track scripts, lockfiles, patches, and README files
so target versions can be recreated without committing large generated content.

Current targets:

- `xpdf/`: reproducible xpdf source fetching and builds for 4.02, 4.03, and 4.06.

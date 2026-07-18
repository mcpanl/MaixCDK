# arm64 middleware libs (optional local override)

Place aarch64 `.so` / `.a` here to override the default cache.

If this directory has no real libs (e.g. only this README), CMake falls back to:

```text
<repo>/MaixArm64Lib/arm64-glibc/cvi_mpi/lib
```

Populate the cache with:

```bash
./scripts/sync_maixcam_arm64_sdk.sh --libs-only
```

Optional: copy or symlink from the cache into this directory:

```bash
LIB_ROOT="$(pwd)/MaixArm64Lib/arm64-glibc"
cp -a "${LIB_ROOT}/cvi_mpi/lib/." \
  MaixCDK/components/3rd_party/cvi-middleware/cvi_mpi_sophgo/lib/arm64/
```

Do not mix riscv64 binaries into this folder.

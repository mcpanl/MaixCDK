# Camera Capture Debug Demo (Zonhor)

Minimal isolation test: open `camera::Camera`, read RGB888 frames for at least 1 second, save the **last** frame to disk. Also dumps DISPLAY and a **diagnostic-only** Group0 NV21→CPU RGB reference. No product display/resize/HUD path.

## Build / deploy

```bash
export MAIXCDK_PATH=/home/satuo/zonhor/maix_arm64/MaixCDK/
cd examples/camera_capture_debug_demo
maixcdk build --build-type=Debug -p zonhor --arch arm64
scp -r dist/ root@192.168.100.141:/root/
```

## Run on device

```bash
imx678-mode 1080p
cd /root/dist/camera_capture_debug_demo_debug_arm64
export LD_LIBRARY_PATH=./dl_lib:/root/dist/fb_display_camera_demo_debug_arm64/dl_lib:$LD_LIBRARY_PATH
./camera_capture_debug_demo 720 720 1000 /root/camera_capture_debug/last.jpg
```

Outputs:
- `/root/camera_capture_debug/last.jpg` — `cam.read()` MAIN_RGB
- `/root/camera_capture_debug/display.jpg` — DISPLAY endpoint
- `/root/camera_capture_debug/g0_cpu_rgb.jpg` — **diagnostic only** CPU NV21→RGB from Group0

## Isolation findings (2026-07-19)

| Artifact | Result |
|----------|--------|
| `last.jpg` (MAIN_RGB `cam.read`) | Abnormal (banding + green/magenta blocks) |
| `display.jpg` (G2-Ch0 RGB) | Abnormal (same class of corruption) |
| `g0_cpu_rgb.jpg` (G0 NV21 + CPU convert) | Structurally recognizable 24-color chart |

### Conclusions

1. Fault is **not** in `fb_display_camera_demo` resize/HUD/FB compositing — pure `cam.read()` already bad.
2. Fault is **not** unique to MAIN_RGB copy — DISPLAY HW RGB is also bad.
3. Sensor / VI / Group0 rotated NV21 path is **healthy** (CPU convert proves it). CPU convert is only a diagnostic, not a product fix.
4. Fault is in the **Group2 hardware RGB CSC / packed RGB output path** (or its inputs into that CSC), not in `Image::save`.

### Runtime metadata notes

- MAIN_RGB reports `fmt=RGB_888(0)`, `stride=align(w*3,64)` (e.g. 720 → 2176), not `buffer_width*3` (768×3=2304).
- Profile `buffer_width` (pixel 64-align) ≠ HW packed-RGB byte stride model.
- Always mmap from `u64PhyAddr` (do not trust `pu8VirAddr` from `GetChnFrame`).

### Experiments that did **not** fix HW RGB

- Skip Group2 input crop
- Enable only G2-Ch1
- Force buffer-aligned `SetChnAttr` (768×720)
- Move Group2 to Device0 + ch0 (sample-like); Device0 rejects some configs with `0xc0068003`

### Next investigation targets

1. Compare working `sample_sensor_lcd` (G0→G1 Dev0 CSC at 192×320) vs current G0→G2 cascade at full rotated 1088×1920 into RGB.
2. Whether large ROT90 NV21 → RGB CSC on this SoC/topology is misconfigured (group id, bind, maxW/H, depth, dual-VPSS mode).
3. Keep CPU NV21→RGB as A/B only; fix must restore HW `PIXEL_FORMAT_RGB_888` path.

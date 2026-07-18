# FB Display + Camera Demo (Zonhor)

Framebuffer UI composition with a live camera thumbnail.  
Targets **Zonhor SG2000** (`PLATFORM_ZONHOR`, arm64): JD9853 panel 172×320 on `/dev/fb0`, IMX678 via `zonhor_mmf`.

## What it demonstrates

- `display::Display(172, 320)` — same orientation as the physical panel
- `camera::Camera(1080, 1920)` — **user portrait coordinates** (not MaixCam's `1920×1080`)
- VPSS **hardware** GDC rotation (no CPU `cv::rotate`)
- HUD overlay (battery / temperature) and corner orientation tags

See [MaixArm64Doc/modules/zonhor_mmf.md](../../../MaixArm64Doc/modules/zonhor_mmf.md) for why Zonhor uses `1080×1920` and how the MMF pipeline is wired.

## Build

```bash
export MAIXCDK_PATH=/path/to/MaixCDK
cd examples/fb_display_camera_demo
maixcdk build --arch arm64
```

Binary: `dist/fb_display_camera_demo_Release_arm64/fb_display_camera_demo`

## Run on device

```bash
# Stop other /dev/fb0 users first (e.g. screen_demo.py, zonhor-ota-ui)
imx678-mode 1080p

./fb_display_camera_demo
./fb_display_camera_demo /dev/fb0
./fb_display_camera_demo -m          # VPSS horizontal mirror
./fb_display_camera_demo -f          # VPSS vertical flip
```

## Expected logs

```text
[zonhor_mmf] Pipeline ready: sensor 1920x1080 -> cam NV21 1920x1088 rot=1 -> NV21 1088x1920 -> RGB 1080x1920 ...
[ZonhorMediaRuntime] init ok (ini=... cam=1080x1920)
camera opened: 1080x1920 (user coords)
```

## Orientation check

1. Hold the device in portrait; on-screen text (TL/TR/BL/BR) should read normally.
2. Move an object along the **bottom** of the camera view (left ↔ right).  
   The thumbnail should show the same **horizontal** motion (not vertical along the edge).

If motion appears rotated 90°, check that `zonhor_mmf` uses NV21+GDC on VPSS0 chn0 (not RGB888 direct).

## Related

- `examples/fb_display_demo` — display only, no camera
- `examples/zonhor_hw_preview` — low-level `ZONHOR_FB_BlitPreview` without `maix.camera`

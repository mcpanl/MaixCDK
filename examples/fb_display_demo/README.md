# FB Display Demo

Framebuffer display test for MaixCDK `z_display` on MaixCAM arm64.

Outputs RGB test pattern to `/dev/fb0` (172x320 panel). Pass a different device path as the first argument if needed.

## Build (arm64)

```bash
export MAIXCDK_PATH=/path/to/MaixCDK
cd examples/fb_display_demo
maixcdk build --arch arm64
```

Binary: `dist/fb_display_demo_Release_arm64/fb_display_demo`

## Run on device

```bash
./fb_display_demo
# or
./fb_display_demo /dev/fb0
```

Press Ctrl+C to exit.

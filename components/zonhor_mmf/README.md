# zonhor_mmf

Zonhor-only media pipeline for **IMX678** + **JD9853** framebuffer LCD (`/dev/fb0`, 172×320 RGB565).  
Derived from SDK `sample_sensor_lcd`, extended for `maix.camera.Camera::read`.

详细踩坑记录见仓库根目录 [MaixArm64Doc/modules/zonhor_mmf.md](../../../MaixArm64Doc/modules/zonhor_mmf.md)。

## Pipeline

```
VI (NV21 1920×1080)
  ├─ VPSS0 chn0  NV21 pre 1920×1088 + ROT90  →  VPSS2 (GRP_CAM_CSC)  →  RGB888 1080×1920  →  Camera::read
  └─ VPSS0 chn1  NV21 320×192 + ROT90        →  VPSS1 (GRP_CSC)       →  RGB888 192×320    →  HW preview blit
```

| VPSS group | ID | Role |
|------------|-----|------|
| `ZONHOR_MMF_GRP_CAM` | 0 | VI in; dual chn (camera + LCD preview) |
| `ZONHOR_MMF_GRP_CSC` | 1 | Preview NV21 → RGB888 |
| `ZONHOR_MMF_GRP_CAM_CSC` | 2 | Camera NV21(+ROT) → RGB888 |

No VO. All groups use `VpssDev=0`.

### Why NV21 + ROT, not RGB888?

GDC (`CVI_VPSS_SetChnRotation`) only supports **NV12/NV21**.  
`ROTATION_90 + RGB888` fails with `CVI_ERR_VPSS_ILLEGAL_PARAM` (`0xc0068003`).  
Do **not** rotate in CPU (`cv::rotate`) on the hot path.

### GDC 64-pixel alignment

Both width and height of the **pre-rotation** channel size must be multiples of 64.

| User request | Pre-rot NV21 | After ROT90 | RGB out |
|--------------|--------------|-------------|---------|
| 1080×1920 (portrait) | 1920×**1088** | 1088×1920 | 1080×1920 |
| 1920×1080 (landscape) | 1920×1088 | — ROT0 — | 1920×1080 |

`1088 = ZONHOR_MMF_VPSS_ALIGN_UP(1080)`.

VB pool [1] sizes NV21 blocks to `max(pre_rot, post_rot)`; `blk_cnt=5` for dual GDC channels.

## User vs ISP coordinates

| Layer | Example 1080p portrait |
|-------|-------------------------|
| User API (`Camera(w,h)`) | 1080 × 1920 |
| ISP sensor | 1920 × 1080 |
| `ZONHOR_MMF_SetCamSize` | accepts **user** w×h |
| `get_sensor_size()` (camera) | returns **user** {1080, 1920} |

On MaixCam, users typically pass `Camera(1920, 1080)` because the display path rotates 90°.  
On Zonhor, screen and camera share product orientation — pass `Camera(1080, 1920)`.

## Vision backend

`PLATFORM_ZONHOR` defaults to `MAIXCAM_VISION_BACKEND=zonhor_mmf`.

Override: `-DMAIXCAM_VISION_BACKEND=x_mmf` (legacy MaixCam topology).

Implementation: `components/vision/port/maixcam_zonhor/`.

## API summary

```c
ZONHOR_MMF_CFG_S cfg;
ZONHOR_MMF_DefaultConfig(&cfg);   // cam 1080×1920, 30fps
cfg.sensor_ini = "/mnt/system/usr/bin/sensor_cfg.ini.imx678_1080p_bin";
ZONHOR_MMF_Init(&cfg);

ZONHOR_MMF_SetCamSize(1080, 1920, 30);  // user coords; portrait → ROT90 inside

VIDEO_FRAME_INFO_S frame;
ZONHOR_MMF_CamGetFrame(&frame, 1000);   // RGB888 from VPSS2
ZONHOR_MMF_CamReleaseFrame(&frame);
```

## HW preview

`ZONHOR_FB_BlitPreview()` — full-screen path matching `sample_sensor_lcd`.  
App composition can use `display::Display` + `FB_Display::show` instead.

## Troubleshooting

| Log / error | Fix |
|-------------|-----|
| `requires 64 alignments` | Use `ZONHOR_MMF_VPSS_ALIGN_UP` on pre-rot dims |
| `Can't acquire VB BLK` | Increase NV21 pool size/count; check ION |
| `CamGetFrame 0xc006800e` | `CVI_ERR_VPSS_BUF_EMPTY` — usually alignment or VB |
| Wrong motion direction | Ensure chn0 NV21 + ROT90, not RGB direct |

## Reference

- `reference/sample_sensor_lcd.c` — SDK host sync
- `reference/sample_sensor_lcd_readme.md` — original LCD sample notes

# zonhor_mmf

Zonhor-only media graph for **IMX678** + **JD9853** framebuffer LCD (`/dev/fb0`, 172×320 RGB565).

Built from the greenfield design in `000_zonhor_mmf_camera_refactor_plan.md`
(profile + graph runtime; not a patch of the old three-group sample).

详细踩坑：
- [`000_zonhor_mmf_camera_refactor_troubleshoot.md`](../../../000_zonhor_mmf_camera_refactor_troubleshoot.md)
- [`000_zonhor_vpss_portrait_nv21_rgb_csc_investigation.md`](../../../000_zonhor_vpss_portrait_nv21_rgb_csc_investigation.md)

## Pipeline

```
Sensor/VI 1920x1080
  -> Group0 / Device0
      Ch0: ROT90 NV21
           user logical 1080x1920
           VPSS buffer  1088x1920   (width 64-align)
          -> Group2 / Device1
              MaxW/H = 1088x1920   (MUST equal G0 buffer; never sensor_w)
              SetGrpCrop = valid   (e.g. 4,0,1080,1920)
              -> ChA: RGB888 display_preview  SetChnAttr 172x320
              -> ChB: RGB888 main_rgb         SetChnAttr user size
              -> ChC: half-size YUV           -> Group3
  Group1 / Device0 / MEM                     create-only
```

| Group | Dev | Role | Default |
|-------|-----|------|---------|
| 0 | 0 | VI → rotate main YUV | enabled |
| 1 | 0 | MEM→MEM reserve | **create only** |
| 2 | 1 | Distribute display / main / half | enabled |
| 3 | 1 | Low-res sub VENC / NPU / reserved | bound, channels **reserved** |

VPSS **Dual** mode, **VI ONLINE + VPSS OFFLINE**.

## Size contract (do not mix)

| Layer | Who uses it | Example (1080p portrait) |
|-------|-------------|--------------------------|
| **logical_*** | User API, ordinary `SetChnAttr` | 1080×1920 |
| **buffer_*** | VB pool, G0 GetFrame wh, **GrpAttr MaxW/H** | 1088×1920 |
| **valid_*** | `SetGrpCrop`, FB blit crop | (4,0,1080,1920) center letterbox |

Helper: `z_gdc_rot90_extent(sensor_w, sensor_h, Z_PAD_CENTER, &ext)`.

### Hard rules (board-proven 2026-07-20)

1. User wants **1080×1920** → G0 still stores **1088×1920**; crop with `SetGrpCrop(valid_*)`.
2. Ordinary channels: `SetChnAttr = logical_*` — **never** write `buffer_*` as output size.
3. VPSS←VPSS `u32MaxW/H` = bound input **buffer_*** — **never** `max(..., sensor_w)`.
   Lifting MaxW to landscape `1920` while input is `1088` wide → HW RGB zipper / neon garbage.
4. G0 GDC: `SetChnAttr` pre-rot `(sensor_w, align64(sensor_h))` + `ASPECT_RATIO_AUTO` + `ROT90`.

### Alignment

- `buffer_width = align_up(logical_width, 64)`
- After 90° rotate, width alignment is **re-checked** (sensor H becomes output W)

## Modules

| File | Layer |
|------|-------|
| `zonhor_frame_layout.*` | extent / align / rotate / `z_gdc_rot90_extent` / crop helpers |
| `zonhor_sensor_mode.*` | IMX678 bin vs 5MP policy (orientation-agnostic) |
| `zonhor_graph_profile.*` | `Group0~3` profile builder + dump |
| `zonhor_graph_runtime.*` | create / bind / MaxW / SetGrpCrop / endpoints |
| `zonhor_mmf.*` | compatible C facade (`Init` / `CamGetFrame` / …) |
| `zonhor_fb_lcd.*` | FB blit with `valid_*` extent |

## IMX678 sensor mode

| Request size (user WxH) | Mode | ini |
|-------------------------|------|-----|
| long≤1920 and short≤1080 (e.g. `1920×1080` or `1080×1920`) | **2×2 binning** | `sensor_cfg.ini.imx678_1080p_bin` |
| larger (up to 2848×1602) | 4K→5MP crop | `sensor_cfg.ini.imx678_5m` |

`Camera(1920,1080)` and `Camera(1080,1920)` are the same 1080p envelope — always prefer binning, never 5MP crop.

Ini resolve order on `ZONHOR_MMF_Init` / `zonhor_graph_open`:

1. env `MAIX_SENSOR_CFG_INI`
2. explicit `cfg.sensor_ini` / `SetSensorIniPath`
3. size-based auto (above table)

## User vs ISP coordinates

| Layer | Example 1080p portrait |
|-------|-------------------------|
| User API (`Camera(w,h)`) | 1080 × 1920 |
| ISP sensor | 1920 × 1080 |
| `ZONHOR_MMF_SetCamSize` | accepts **user** w×h |
| `get_sensor_size()` | returns **user** {1080, 1920} |

## Vision backend

`PLATFORM_ZONHOR` defaults to `MAIXCAM_VISION_BACKEND=zonhor_mmf`.

Implementation: `components/vision/port/maixcam_zonhor/`.

## API summary

```c
ZONHOR_MMF_CFG_S cfg;
ZONHOR_MMF_DefaultConfig(&cfg);
/* sensor_ini NULL → auto imx678_1080p_bin for default 1080x1920 */
ZONHOR_MMF_Init(&cfg);   // builds profile, Dual VPSS, Group0~3

z_camera_output_desc_t d;
ZONHOR_MMF_GetOutputDesc(Z_CAMERA_OUTPUT_DISPLAY, &d); // 172/192 extents

VIDEO_FRAME_INFO_S frame;
ZONHOR_MMF_CamGetFrame(&frame, 1000);      // Group2-ChB RGB
ZONHOR_MMF_PreviewGetFrame(&frame, 1000);  // Group2-ChA RGB

zonhor_graph_enable_output(Z_CAMERA_OUTPUT_SUB_VENC); // reserved → enabled
```

## HW preview

`ZONHOR_FB_BlitPreview()` crops `valid_*` from the buffer then blits to 172×320.

## Reference

- `reference/sample_sensor_lcd.c` — small aligned ROT path (320×192, no GrpCrop)
- Repo plan / investigation docs at repo root `000_zonhor_*.md`

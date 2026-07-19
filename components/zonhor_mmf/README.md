# zonhor_mmf

Zonhor-only media graph for **IMX678** + **JD9853** framebuffer LCD (`/dev/fb0`, 172×320 RGB565).

Built from the greenfield design in `000_zonhor_mmf_camera_refactor_plan.md`
(profile + graph runtime; not a patch of the old three-group sample).

详细踩坑记录见 [MaixArm64Doc/modules/zonhor_mmf.md](../../../MaixArm64Doc/modules/zonhor_mmf.md)。

## Pipeline

```
Sensor/VI
  -> Group0 / Device0 / VI input
      -> Ch0: ROT90 YUV (rotated_main_yuv), width 64-aligned after rotate
          -> Group2 / Device1
              -> ChA: RGB888 display_preview  logical 172x320 / buffer 192x320
              -> ChB: RGB888 main_rgb         Camera::read / main_venc_input
              -> ChC: half-size YUV           -> Group3 / Device1 (reserved)
  Group1 / Device0 / MEM                     create-only (user_mem_pipeline)
```

| Group | Dev | Role | Default |
|-------|-----|------|---------|
| 0 | 0 | VI → rotate main YUV | enabled |
| 1 | 0 | MEM→MEM reserve | **create only** |
| 2 | 1 | Distribute display / main / half | enabled |
| 3 | 1 | Low-res sub VENC / NPU / reserved | bound, channels **reserved** |

VPSS **Dual** mode, **VI ONLINE + VPSS OFFLINE**.

### Alignment rules

All outputs go through `z_frame_layout_calc` / `z_rotate_extent`:

- `buffer_width = align_up(logical_width, 64)`
- After 90° rotate, width alignment is **re-checked** (sensor H becomes output W)

| User request | Sensor | Group0 post-rot buffer | Display buffer |
|--------------|--------|------------------------|----------------|
| 1080×1920 | 1920×1080 | 1088×1920 | 192×320 (valid 172×320 @ x=10) |

### Modules

| File | Layer |
|------|-------|
| `zonhor_frame_layout.*` | extent / align / rotate / half |
| `zonhor_graph_profile.*` | `Group0~3` profile builder + dump |
| `zonhor_graph_runtime.*` | create / bind / start / stop / endpoints |
| `zonhor_mmf.*` | compatible C facade (`Init` / `CamGetFrame` / …) |
| `zonhor_fb_lcd.*` | FB blit with `valid_*` extent |

Named endpoints: `display_preview`, `main_venc_input` / `main_rgb`,
`sub_pipeline_input`, `sub_venc_input`, `sub_npu_input`, `user_mem_pipeline`.

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
cfg.sensor_ini = "/mnt/system/usr/bin/sensor_cfg.ini.imx678_1080p_bin";
ZONHOR_MMF_Init(&cfg);   // builds profile, Dual VPSS, Group0~3

z_camera_output_desc_t d;
ZONHOR_MMF_GetOutputDesc(Z_CAMERA_OUTPUT_DISPLAY, &d); // 172/192 extents

VIDEO_FRAME_INFO_S frame;
ZONHOR_MMF_CamGetFrame(&frame, 1000);      // Group2-ChB RGB
ZONHOR_MMF_PreviewGetFrame(&frame, 1000);  // Group2-ChA RGB

zonhor_graph_enable_output(Z_CAMERA_OUTPUT_SUB_VENC); // reserved → enabled
```

## HW preview

`ZONHOR_FB_BlitPreview()` crops `valid_*` from the 192-wide buffer then blits to 172×320.

## Reference

- `reference/sample_sensor_lcd.c` — historical SDK sample (old topology)
- Repo plan: `000_zonhor_mmf_camera_refactor_plan.md`

/**
 * Zonhor-specific MMF pipeline (IMX678 + JD9853 FB LCD).
 *
 * Topology:
 *   VI (NV21)
 *     → VPSS0 chn0  NV21 WxH + ROT90 (portrait) → VPSS2 CSC RGB888 → Camera::read
 *     → VPSS0 chn1  NV21 320x192 ROT90          → VPSS1 CSC RGB888 → HW LCD preview
 *
 * GDC rotation requires NV21 (not RGB888). No VO. VPSS groups use VpssDev 0.
 */
#ifndef __ZONHOR_MMF_H__
#define __ZONHOR_MMF_H__

#include <stddef.h>
#include <stdint.h>

#include "sample_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZONHOR_MMF_VPSS_ALIGN     64
#define ZONHOR_MMF_VPSS_ALIGN_UP(x) \
	((((x) + ZONHOR_MMF_VPSS_ALIGN - 1) / ZONHOR_MMF_VPSS_ALIGN) * ZONHOR_MMF_VPSS_ALIGN)

#define ZONHOR_MMF_PRE_W          320
#define ZONHOR_MMF_PRE_H          ZONHOR_MMF_VPSS_ALIGN_UP(172) /* 192 */
#define ZONHOR_MMF_DISP_W         ZONHOR_MMF_PRE_H              /* 192 */
#define ZONHOR_MMF_DISP_H         ZONHOR_MMF_PRE_W              /* 320 */

#define ZONHOR_MMF_GRP_CAM        0 /* VI → dual-chn */
#define ZONHOR_MMF_GRP_CSC        1 /* preview NV21 → RGB888 */
#define ZONHOR_MMF_GRP_CAM_CSC    2 /* camera NV21+ROT → RGB888 */
#define ZONHOR_MMF_CHN_CAM        0
#define ZONHOR_MMF_CHN_ROT        1

#define ZONHOR_FB_LCD_WIDTH       172
#define ZONHOR_FB_LCD_HEIGHT      320
#define ZONHOR_FB_LCD_DEV         "/dev/fb0"

typedef struct {
	CVI_U32 cam_w;          /* Camera RGB output width (user coords, default 1080) */
	CVI_U32 cam_h;          /* Camera RGB output height (user coords, default 1920) */
	CVI_S32 cam_fps;        /* Channel frame rate, -1 = unlimited */
	CVI_BOOL mirror;        /* Applied on ROT chn (and cam chn) */
	CVI_BOOL flip;
	const char *sensor_ini; /* Optional; env MAIX_SENSOR_CFG_INI wins */
} ZONHOR_MMF_CFG_S;

void ZONHOR_MMF_DefaultConfig(ZONHOR_MMF_CFG_S *cfg);

/** Programmatic sensor_cfg.ini path (before Init). Env MAIX_SENSOR_CFG_INI wins. */
void ZONHOR_MMF_SetSensorIniPath(const char *ini_path);
const char *ZONHOR_MMF_GetSensorIniPath(void);

CVI_S32 ZONHOR_MMF_Init(const ZONHOR_MMF_CFG_S *cfg);
void ZONHOR_MMF_Deinit(void);
CVI_BOOL ZONHOR_MMF_IsInited(void);

void ZONHOR_MMF_GetSensorSize(CVI_U32 *w, CVI_U32 *h);
void ZONHOR_MMF_GetCamSize(CVI_U32 *w, CVI_U32 *h);

/** Resize camera RGB output (user coords); VPSS applies ROT90 when portrait. */
CVI_S32 ZONHOR_MMF_SetCamSize(CVI_U32 w, CVI_U32 h, CVI_S32 fps);

CVI_S32 ZONHOR_MMF_SetMirrorFlip(CVI_BOOL mirror, CVI_BOOL flip);

CVI_S32 ZONHOR_MMF_CamGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 ZONHOR_MMF_CamReleaseFrame(VIDEO_FRAME_INFO_S *frame);

CVI_S32 ZONHOR_MMF_PreviewGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 ZONHOR_MMF_PreviewReleaseFrame(VIDEO_FRAME_INFO_S *frame);

/* ── Framebuffer helpers (optional HW preview path) ─────────────────────── */

typedef struct {
	int fd;
	uint16_t *fb;
	size_t size;
	int width;
	int height;
	int line_length;
} ZONHOR_FB_LCD_S;

int ZONHOR_FB_Open(ZONHOR_FB_LCD_S *fb, const char *dev);
void ZONHOR_FB_Close(ZONHOR_FB_LCD_S *fb);
void ZONHOR_FB_Clear(ZONHOR_FB_LCD_S *fb, uint16_t color);
void ZONHOR_FB_DrawRgb565(ZONHOR_FB_LCD_S *fb, const uint16_t *src, int src_w, int src_h);
void ZONHOR_FB_DrawStatusHud(ZONHOR_FB_LCD_S *fb, int bat_valid, int bat_pct,
			     int temp_valid, int temp_c);

/** Packed RGB888 VIDEO_FRAME → RGB565 (1:1, uses stride). */
int ZONHOR_RGB888_ToRgb565(const VIDEO_FRAME_S *frame, uint16_t *dst);

/**
 * One-shot: PreviewGetFrame → RGB565 → center-crop blit → Release.
 * Returns 0 on success, negative on failure / timeout skip.
 */
int ZONHOR_FB_BlitPreview(ZONHOR_FB_LCD_S *fb, uint16_t *rgb565_scratch,
			  size_t scratch_pixels, CVI_S32 timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_MMF_H__ */

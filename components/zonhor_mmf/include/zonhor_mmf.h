/**
 * Zonhor MMF public API — thin facade over the profile-driven graph runtime.
 *
 * Size contract (see zonhor_frame_layout.h / 000_zonhor_vpss_portrait_nv21_rgb_csc_investigation.md):
 *   User / Camera API     : logical  e.g. 1080x1920
 *   G0 GDC storage        : buffer   e.g. 1088x1920  (width 64-align)
 *   G2 GrpAttr MaxW/H     : = G0 buffer (never lift to landscape sensor_w)
 *   G2 SetGrpCrop         : valid_* crop of logical inside that buffer
 *   G2 SetChnAttr (RGB)   : logical size (never write buffer_* as output size)
 *
 * Topology:
 *   VI -> Group0(Dev0) ROT90 YUV
 *       -> Group2(Dev1) ChA display RGB 172x320
 *                     ChB main RGB (Camera::read)
 *                     ChC half YUV -> Group3 reserved
 *   Group1(Dev0) MEM create-only
 */
#ifndef __ZONHOR_MMF_H__
#define __ZONHOR_MMF_H__

#include <stddef.h>
#include <stdint.h>

#include "sample_comm.h"
#include "zonhor_frame_layout.h"
#include "zonhor_graph_profile.h"
#include "zonhor_graph_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZONHOR_MMF_VPSS_ALIGN     64
#define ZONHOR_MMF_VPSS_ALIGN_UP(x) \
	((((x) + ZONHOR_MMF_VPSS_ALIGN - 1) / ZONHOR_MMF_VPSS_ALIGN) * ZONHOR_MMF_VPSS_ALIGN)

#define ZONHOR_FB_LCD_WIDTH       172
#define ZONHOR_FB_LCD_HEIGHT      320
#define ZONHOR_FB_LCD_DEV         "/dev/fb0"

/* Legacy preview buffer macros (logical 172, buffer 192). */
#define ZONHOR_MMF_PRE_W          320
#define ZONHOR_MMF_PRE_H          ZONHOR_MMF_VPSS_ALIGN_UP(172) /* 192 */
#define ZONHOR_MMF_DISP_W         ZONHOR_MMF_PRE_H              /* 192 */
#define ZONHOR_MMF_DISP_H         ZONHOR_MMF_PRE_W              /* 320 */

/* Legacy group id aliases mapped onto the new graph. */
#define ZONHOR_MMF_GRP_CAM        0 /* Group0 */
#define ZONHOR_MMF_GRP_CSC        2 /* Group2 (display lives on ChA) */
#define ZONHOR_MMF_GRP_CAM_CSC    2 /* Group2 (main RGB on ChB) */
#define ZONHOR_MMF_CHN_CAM        0
#define ZONHOR_MMF_CHN_ROT        0
#define ZONHOR_MMF_CHN_DISP       0
#define ZONHOR_MMF_CHN_MAIN       1

typedef struct {
	CVI_U32 cam_w;          /* Camera RGB output width (user coords, default 1080) */
	CVI_U32 cam_h;          /* Camera RGB output height (user coords, default 1920) */
	CVI_S32 cam_fps;        /* Channel frame rate, -1 = unlimited */
	CVI_BOOL mirror;        /* Applied on Group0 rotate channel */
	CVI_BOOL flip;
	/*
	 * Optional sensor_cfg.ini. Priority: env MAIX_SENSOR_CFG_INI >
	 * this field / SetSensorIniPath > size-based auto
	 * (≤1080p envelope → 2x2 binning, else 5MP crop).
	 */
	const char *sensor_ini;
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

/** Resize camera RGB output (user coords); Group2-ChB. */
CVI_S32 ZONHOR_MMF_SetCamSize(CVI_U32 w, CVI_U32 h, CVI_S32 fps);

CVI_S32 ZONHOR_MMF_SetMirrorFlip(CVI_BOOL mirror, CVI_BOOL flip);

CVI_S32 ZONHOR_MMF_CamGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 ZONHOR_MMF_CamReleaseFrame(VIDEO_FRAME_INFO_S *frame);

CVI_S32 ZONHOR_MMF_PreviewGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 ZONHOR_MMF_PreviewReleaseFrame(VIDEO_FRAME_INFO_S *frame);

/** Query named output extent (logical/buffer/valid). */
CVI_S32 ZONHOR_MMF_GetOutputDesc(z_camera_output_id_t id, z_camera_output_desc_t *desc);

/** Enable/disable reserved graph output (e.g. SUB_VENC before VPSS bind). */
CVI_S32 ZONHOR_MMF_EnableOutput(z_camera_output_id_t id);
CVI_S32 ZONHOR_MMF_DisableOutput(z_camera_output_id_t id);

/** Map encoder output id to VPSS grp/chn for SAMPLE_COMM_VPSS_Bind_VENC. */
CVI_S32 ZONHOR_MMF_GetVencBindInfo(z_camera_output_id_t id, VPSS_GRP *grp,
				   VPSS_CHN *chn, z_camera_output_desc_t *desc);

/* ── Minimal VENC wrapper (Step1) ───────────────────────────────────────── */
/* Currently only supports PT_H264 + CBR (VB_SOURCE_USER + private VB pool). */
#define ZONHOR_MMF_MAX_VENC_CHN        4
#define ZONHOR_MMF_VENC_MAX_PACKS      16

typedef struct {
	VENC_CHN chn;
	PAYLOAD_TYPE_E payload;
	CVI_U32 width;
	CVI_U32 height;
	CVI_U32 fps;
	CVI_U32 gop;
	CVI_U32 bitrate_kbps;
	CVI_BOOL cbr;
	/* Bind input id:
	 * - Optional convenience for callers.
	 * - Actual binding is performed by ZONHOR_MMF_VencBindInput().
	 */
	CVI_BOOL bind_input;
	z_camera_output_id_t input_id;
} ZONHOR_MMF_VENC_CFG_S;

typedef struct {
	VENC_STREAM_S stream;
	VENC_PACK_S packs[ZONHOR_MMF_VENC_MAX_PACKS];
} ZONHOR_MMF_VENC_STREAM_S;

/* Create + StartRecvFrame (create-only). */
CVI_S32 ZONHOR_MMF_VencCreate(const ZONHOR_MMF_VENC_CFG_S *cfg);
CVI_S32 ZONHOR_MMF_VencDestroy(VENC_CHN chn);

/* Bind VPSS input to VENC:
 * Order is handled inside:
 *   StopRecvFrame -> VPSS_Bind_VENC -> StartRecvFrame
 *
 * Practical depth expectation on this platform:
 * - bind时把对应 VPSS depth 设为 0，避免和用户 GetChnFrame 路径互抢。
 */
CVI_S32 ZONHOR_MMF_VencBindInput(VENC_CHN chn, z_camera_output_id_t id);
CVI_S32 ZONHOR_MMF_VencUnbindInput(VENC_CHN chn, z_camera_output_id_t id);

/* User SendFrame path (may be used by later z_video_zonhor steps). */
CVI_S32 ZONHOR_MMF_VencSendFrame(VENC_CHN chn, const VIDEO_FRAME_INFO_S *frame,
				   CVI_S32 timeout_ms);

/* Copy user NV21 buffer into VENC private VB pool block, then SendFrame. */
CVI_S32 ZONHOR_MMF_VencSendNv21UserData(VENC_CHN chn, const CVI_U8 *data,
					 CVI_U32 width, CVI_U32 height,
					 CVI_S32 timeout_ms);

/* QueryStatus -> GetStream. Caller should call VencReleaseStream after use. */
CVI_S32 ZONHOR_MMF_VencGetStream(VENC_CHN chn, ZONHOR_MMF_VENC_STREAM_S *out,
				  CVI_S32 timeout_ms);
CVI_S32 ZONHOR_MMF_VencReleaseStream(VENC_CHN chn,
					ZONHOR_MMF_VENC_STREAM_S *stream);

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
/**
 * Blit RGB565 using valid extent inside a possibly stride-padded buffer.
 * For display_preview, SetChnAttr is logical 172x320; storage/stride may be
 * wider, with content left-aligned at valid_x=0.
 */
void ZONHOR_FB_DrawRgb565Extent(ZONHOR_FB_LCD_S *fb, const uint16_t *src,
				int buffer_w, int buffer_h,
				int valid_x, int valid_y, int valid_w, int valid_h);
void ZONHOR_FB_DrawStatusHud(ZONHOR_FB_LCD_S *fb, int bat_valid, int bat_pct,
			     int temp_valid, int temp_c);

/** Packed RGB888 VIDEO_FRAME → RGB565 (1:1, uses stride); full buffer width. */
int ZONHOR_RGB888_ToRgb565(const VIDEO_FRAME_S *frame, uint16_t *dst);

/**
 * RGB888 frame → RGB565 using only the valid logical region described by extent.
 * dst is sized valid_w * valid_h.
 */
int ZONHOR_RGB888_ToRgb565Extent(const VIDEO_FRAME_S *frame, const z_frame_extent_t *extent,
				 uint16_t *dst);

/**
 * One-shot: PreviewGetFrame → crop valid logical region → RGB565 → blit.
 * Returns 0 on success, negative on failure / timeout skip.
 */
int ZONHOR_FB_BlitPreview(ZONHOR_FB_LCD_S *fb, uint16_t *rgb565_scratch,
			  size_t scratch_pixels, CVI_S32 timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_MMF_H__ */

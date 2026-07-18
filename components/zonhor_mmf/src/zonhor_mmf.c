/**
 * Zonhor MMF — IMX678 pipeline based on sample_sensor_lcd, extended with
 * VPSS0 chn0 NV21+ROT → VPSS2 CSC for Camera::read (user portrait coords).
 */

#include "zonhor_mmf.h"

#include "cvi_buffer.h"
#include "cvi_sys.h"
#include "cvi_vb.h"
#include "cvi_vpss.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZLOGI(fmt, ...) printf("[zonhor_mmf] " fmt "\n", ##__VA_ARGS__)
#define ZLOGE(fmt, ...) printf("[zonhor_mmf] ERROR: " fmt "\n", ##__VA_ARGS__)

static SAMPLE_VI_CONFIG_S g_stViConfig;
static SIZE_S g_stSensorSize;
static CVI_U32 g_cam_w = 1080;
static CVI_U32 g_cam_h = 1920;
static CVI_S32 g_cam_fps = 30;
static CVI_BOOL g_mirror = CVI_FALSE;
static CVI_BOOL g_flip = CVI_FALSE;

static CVI_BOOL g_sys_inited = CVI_FALSE;
static CVI_BOOL g_vi_inited = CVI_FALSE;
static CVI_BOOL g_vpss0_started = CVI_FALSE;
static CVI_BOOL g_vpss1_started = CVI_FALSE;
static CVI_BOOL g_vpss_cam_csc_started = CVI_FALSE;
static CVI_BOOL g_vi_vpss_bound = CVI_FALSE;
static CVI_BOOL g_vpss_vpss_bound = CVI_FALSE;
static CVI_BOOL g_vpss_cam_csc_bound = CVI_FALSE;
static CVI_BOOL g_vpss0_chn_enabled[VPSS_MAX_PHY_CHN_NUM] = {0};
static CVI_BOOL g_vpss1_chn_enabled[VPSS_MAX_PHY_CHN_NUM] = {0};
static CVI_BOOL g_vpss_cam_csc_chn_enabled[VPSS_MAX_PHY_CHN_NUM] = {0};

static char g_sensor_ini_path[256] = {0};

static CVI_BOOL cam_is_portrait(CVI_U32 w, CVI_U32 h)
{
	return w < h;
}

static ROTATION_E cam_rotation(CVI_U32 w, CVI_U32 h)
{
	return cam_is_portrait(w, h) ? ROTATION_90 : ROTATION_0;
}

/* VPSS chn attr size before GDC rotation (landscape leg, 64-aligned). */
static void cam_nv21_pre_rot_size(CVI_U32 user_w, CVI_U32 user_h,
				  CVI_U32 *pre_w, CVI_U32 *pre_h)
{
	if (cam_is_portrait(user_w, user_h)) {
		*pre_w = ZONHOR_MMF_VPSS_ALIGN_UP(user_h);
		*pre_h = ZONHOR_MMF_VPSS_ALIGN_UP(user_w);
	} else {
		*pre_w = ZONHOR_MMF_VPSS_ALIGN_UP(user_w);
		*pre_h = ZONHOR_MMF_VPSS_ALIGN_UP(user_h);
	}
}

static void cam_nv21_post_rot_size(CVI_U32 pre_w, CVI_U32 pre_h, ROTATION_E rot,
				   CVI_U32 *post_w, CVI_U32 *post_h)
{
	if (rot == ROTATION_90 || rot == ROTATION_270) {
		*post_w = pre_h;
		*post_h = pre_w;
	} else {
		*post_w = pre_w;
		*post_h = pre_h;
	}
}

static CVI_U32 vb_pool_blk_size(CVI_U32 w, CVI_U32 h, PIXEL_FORMAT_E fmt)
{
	return COMMON_GetPicBufferSize(w, h, fmt, DATA_BITWIDTH_8,
				       COMPRESS_MODE_NONE, DEFAULT_ALIGN);
}

static void vpss_try_destroy(VPSS_GRP grp)
{
	CVI_S32 j;

	for (j = 0; j < VPSS_MAX_PHY_CHN_NUM; j++)
		CVI_VPSS_DisableChn(grp, j);
	CVI_VPSS_StopGrp(grp);
	CVI_VPSS_DestroyGrp(grp);
}

static void apply_ini_path_override(void)
{
	const char *path = getenv("MAIX_SENSOR_CFG_INI");

	if (!path || path[0] == '\0')
		path = g_sensor_ini_path[0] ? g_sensor_ini_path : NULL;
	if (!path || path[0] == '\0')
		return;

	CVI_S32 ret = SAMPLE_COMM_VI_SetIniPath(path);
	if (ret != CVI_SUCCESS)
		ZLOGE("SAMPLE_COMM_VI_SetIniPath(%s) failed: 0x%x", path, ret);
	else
		ZLOGI("sensor ini override: %s", path);
}

void ZONHOR_MMF_DefaultConfig(ZONHOR_MMF_CFG_S *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->cam_w = 1080;
	cfg->cam_h = 1920;
	cfg->cam_fps = 30;
	cfg->mirror = CVI_FALSE;
	cfg->flip = CVI_FALSE;
	cfg->sensor_ini = NULL;
}

void ZONHOR_MMF_SetSensorIniPath(const char *ini_path)
{
	if (!ini_path || ini_path[0] == '\0') {
		g_sensor_ini_path[0] = '\0';
		return;
	}
	snprintf(g_sensor_ini_path, sizeof(g_sensor_ini_path), "%s", ini_path);
}

const char *ZONHOR_MMF_GetSensorIniPath(void)
{
	const char *env = getenv("MAIX_SENSOR_CFG_INI");

	if (env && env[0])
		return env;
	return g_sensor_ini_path;
}

CVI_BOOL ZONHOR_MMF_IsInited(void)
{
	return g_sys_inited && g_vi_inited && g_vpss0_started &&
	       g_vpss1_started && g_vpss_cam_csc_started;
}

void ZONHOR_MMF_GetSensorSize(CVI_U32 *w, CVI_U32 *h)
{
	if (w)
		*w = g_stSensorSize.u32Width;
	if (h)
		*h = g_stSensorSize.u32Height;
}

void ZONHOR_MMF_GetCamSize(CVI_U32 *w, CVI_U32 *h)
{
	if (w)
		*w = g_cam_w;
	if (h)
		*h = g_cam_h;
}

void ZONHOR_MMF_Deinit(void)
{
	if (g_vpss_cam_csc_bound) {
		SAMPLE_COMM_VPSS_UnBind_VPSS(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM,
					    ZONHOR_MMF_GRP_CAM_CSC);
		g_vpss_cam_csc_bound = CVI_FALSE;
	}
	if (g_vpss_vpss_bound) {
		SAMPLE_COMM_VPSS_UnBind_VPSS(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_ROT,
					    ZONHOR_MMF_GRP_CSC);
		g_vpss_vpss_bound = CVI_FALSE;
	}
	if (g_vi_vpss_bound) {
		SAMPLE_COMM_VI_UnBind_VPSS(0, 0, ZONHOR_MMF_GRP_CAM);
		g_vi_vpss_bound = CVI_FALSE;
	}
	if (g_vpss1_started) {
		SAMPLE_COMM_VPSS_Stop(ZONHOR_MMF_GRP_CSC, g_vpss1_chn_enabled);
		g_vpss1_started = CVI_FALSE;
		memset(g_vpss1_chn_enabled, 0, sizeof(g_vpss1_chn_enabled));
	} else {
		vpss_try_destroy(ZONHOR_MMF_GRP_CSC);
	}
	if (g_vpss_cam_csc_started) {
		SAMPLE_COMM_VPSS_Stop(ZONHOR_MMF_GRP_CAM_CSC, g_vpss_cam_csc_chn_enabled);
		g_vpss_cam_csc_started = CVI_FALSE;
		memset(g_vpss_cam_csc_chn_enabled, 0, sizeof(g_vpss_cam_csc_chn_enabled));
	} else {
		vpss_try_destroy(ZONHOR_MMF_GRP_CAM_CSC);
	}
	if (g_vpss0_started) {
		SAMPLE_COMM_VPSS_Stop(ZONHOR_MMF_GRP_CAM, g_vpss0_chn_enabled);
		g_vpss0_started = CVI_FALSE;
		memset(g_vpss0_chn_enabled, 0, sizeof(g_vpss0_chn_enabled));
	} else {
		vpss_try_destroy(ZONHOR_MMF_GRP_CAM);
	}

	if (g_vi_inited) {
		SAMPLE_COMM_VI_DestroyIsp(&g_stViConfig);
		SAMPLE_COMM_VI_DestroyVi(&g_stViConfig);
		g_vi_inited = CVI_FALSE;
	}
	if (g_sys_inited) {
		SAMPLE_COMM_SYS_Exit();
		g_sys_inited = CVI_FALSE;
	}
	ZLOGI("deinit ok");
}

CVI_S32 ZONHOR_MMF_Init(const ZONHOR_MMF_CFG_S *cfg)
{
	MMF_VERSION_S stVersion;
	SAMPLE_INI_CFG_S stIniCfg;
	SAMPLE_VI_CONFIG_S stViConfig;
	CVI_S32 s32Ret;
	LOG_LEVEL_CONF_S log_conf;
	VB_CONFIG_S stVbConf;
	CVI_U32 u32ViBlk, u32ViRotBlk, u32CamNv21Blk, u32CamRgbBlk, u32Nv21Blk, u32Nv21RotBlk, u32LcdRgbBlk;
	CVI_U32 cam_pre_w, cam_pre_h, cam_post_w, cam_post_h;
	ROTATION_E cam_rot;
	VPSS_GRP_ATTR_S stVpssGrpAttr;
	VPSS_CHN_ATTR_S astVpssChnAttr[VPSS_MAX_PHY_CHN_NUM];
	CVI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};
	ZONHOR_MMF_CFG_S local;

	if (ZONHOR_MMF_IsInited()) {
		ZLOGI("already inited");
		return CVI_SUCCESS;
	}

	if (cfg)
		local = *cfg;
	else
		ZONHOR_MMF_DefaultConfig(&local);

	if (local.cam_w == 0)
		local.cam_w = 1080;
	if (local.cam_h == 0)
		local.cam_h = 1920;
	if (local.cam_fps == 0)
		local.cam_fps = 30;

	g_cam_w = local.cam_w;
	g_cam_h = local.cam_h;
	g_cam_fps = local.cam_fps;
	g_mirror = local.mirror;
	g_flip = local.flip;

	if (local.sensor_ini && local.sensor_ini[0])
		ZONHOR_MMF_SetSensorIniPath(local.sensor_ini);

	CVI_SYS_GetVersion(&stVersion);
	ZLOGI("MMF Version:%s", stVersion.version);

	log_conf.enModId = CVI_ID_LOG;
	log_conf.s32Level = CVI_DBG_INFO;
	CVI_LOG_SetLevelConf(&log_conf);

	apply_ini_path_override();

	s32Ret = SAMPLE_COMM_VI_ParseIni(&stIniCfg);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("ParseIni fail: 0x%x", s32Ret);
		return s32Ret;
	}

	{
		SAMPLE_SNS_MODE_INFO_S stModeInfo;

		s32Ret = SAMPLE_COMM_SNS_QueryActiveMode(&stIniCfg, &stModeInfo);
		if (s32Ret != CVI_SUCCESS) {
			ZLOGE("QueryActiveMode fail: 0x%x", s32Ret);
			return s32Ret;
		}
		g_stSensorSize = stModeInfo.stSize;
		ZLOGI("Sensor mode=%s size=%ux%u raw=%ubit snsMode=%u bin=%s",
		      stModeInfo.pszModeName,
		      stModeInfo.stSize.u32Width, stModeInfo.stSize.u32Height,
		      stModeInfo.u8RawBitDepth, stModeInfo.u8SnsMode,
		      stModeInfo.pszIspBinPath ? stModeInfo.pszIspBinPath : "(default)");
	}

	CVI_VI_SetDevNum(stIniCfg.devNum);

	s32Ret = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, &stViConfig);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("IniToViCfg fail: 0x%x", s32Ret);
		return s32Ret;
	}
	memcpy(&g_stViConfig, &stViConfig, sizeof(SAMPLE_VI_CONFIG_S));

	memset(&stVbConf, 0, sizeof(stVbConf));
	u32ViBlk = vb_pool_blk_size(g_stSensorSize.u32Width, g_stSensorSize.u32Height,
				    stViConfig.astViInfo[0].stChnInfo.enPixFormat);
	u32ViRotBlk = vb_pool_blk_size(g_stSensorSize.u32Height, g_stSensorSize.u32Width,
				       stViConfig.astViInfo[0].stChnInfo.enPixFormat);
	u32ViBlk = u32ViBlk > u32ViRotBlk ? u32ViBlk : u32ViRotBlk;

	cam_nv21_pre_rot_size(g_cam_w, g_cam_h, &cam_pre_w, &cam_pre_h);
	cam_rot = cam_rotation(g_cam_w, g_cam_h);
	cam_nv21_post_rot_size(cam_pre_w, cam_pre_h, cam_rot, &cam_post_w, &cam_post_h);

	{
		CVI_U32 pre_blk = vb_pool_blk_size(cam_pre_w, cam_pre_h, SAMPLE_PIXEL_FORMAT);
		CVI_U32 post_blk = vb_pool_blk_size(cam_post_w, cam_post_h, SAMPLE_PIXEL_FORMAT);

		u32CamNv21Blk = pre_blk > post_blk ? pre_blk : post_blk;
	}
	u32CamRgbBlk = vb_pool_blk_size(g_cam_w, g_cam_h, PIXEL_FORMAT_RGB_888);

	u32Nv21Blk = vb_pool_blk_size(ZONHOR_MMF_PRE_W, ZONHOR_MMF_PRE_H, SAMPLE_PIXEL_FORMAT);
	u32Nv21RotBlk = vb_pool_blk_size(ZONHOR_MMF_DISP_W, ZONHOR_MMF_DISP_H, SAMPLE_PIXEL_FORMAT);
	u32Nv21Blk = u32Nv21Blk > u32Nv21RotBlk ? u32Nv21Blk : u32Nv21RotBlk;

	u32LcdRgbBlk = vb_pool_blk_size(ZONHOR_MMF_DISP_W, ZONHOR_MMF_DISP_H, PIXEL_FORMAT_RGB_888);

	/* Large 5MP RGB888 frames pressure ION/CMA — keep blk counts lean. */
	{
		CVI_U32 cam_pixels = g_cam_w * g_cam_h;
		CVI_U32 vi_cnt = 4;
		/* GDC rot on chn0+chn1 needs more NV21 blocks in flight. */
		CVI_U32 cam_nv21_cnt = 5;
		CVI_U32 cam_cnt = (cam_pixels > (1920u * 1080u)) ? 2 : 3;

		stVbConf.u32MaxPoolCnt = 5;
		stVbConf.astCommPool[0].u32BlkSize = u32ViBlk;
		stVbConf.astCommPool[0].u32BlkCnt = vi_cnt;
		stVbConf.astCommPool[0].enRemapMode = VB_REMAP_MODE_CACHED;
		stVbConf.astCommPool[1].u32BlkSize = u32CamNv21Blk;
		stVbConf.astCommPool[1].u32BlkCnt = cam_nv21_cnt;
		stVbConf.astCommPool[1].enRemapMode = VB_REMAP_MODE_CACHED;
		stVbConf.astCommPool[2].u32BlkSize = u32CamRgbBlk;
		stVbConf.astCommPool[2].u32BlkCnt = cam_cnt;
		stVbConf.astCommPool[2].enRemapMode = VB_REMAP_MODE_CACHED;
		stVbConf.astCommPool[3].u32BlkSize = u32Nv21Blk;
		stVbConf.astCommPool[3].u32BlkCnt = 4;
		stVbConf.astCommPool[3].enRemapMode = VB_REMAP_MODE_CACHED;
		stVbConf.astCommPool[4].u32BlkSize = u32LcdRgbBlk;
		stVbConf.astCommPool[4].u32BlkCnt = 3;
		stVbConf.astCommPool[4].enRemapMode = VB_REMAP_MODE_CACHED;

		ZLOGI("VB[0] VI %ux%u size=%u cnt=%u", g_stSensorSize.u32Width, g_stSensorSize.u32Height,
		      u32ViBlk, vi_cnt);
		ZLOGI("VB[1] cam NV21 pre %ux%u post %ux%u blk=%u cnt=%u",
		      cam_pre_w, cam_pre_h, cam_post_w, cam_post_h, u32CamNv21Blk, cam_nv21_cnt);
		ZLOGI("VB[2] cam RGB %ux%u size=%u cnt=%u", g_cam_w, g_cam_h, u32CamRgbBlk, cam_cnt);
		ZLOGI("VB[3] preview NV21 %dx%d size=%u cnt=4", ZONHOR_MMF_PRE_W, ZONHOR_MMF_PRE_H, u32Nv21Blk);
		ZLOGI("VB[4] preview RGB %dx%d size=%u cnt=3", ZONHOR_MMF_DISP_W, ZONHOR_MMF_DISP_H, u32LcdRgbBlk);
	}

	s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("SYS_Init failed: 0x%x", s32Ret);
		return s32Ret;
	}
	g_sys_inited = CVI_TRUE;

	s32Ret = SAMPLE_PLAT_VI_INIT(&stViConfig);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("VI init failed: 0x%x — try zonhor-cam-recover reload / free ION", s32Ret);
		/* SAMPLE_PLAT_VI_INIT may already tear down MMF; still Exit if we own SYS. */
		if (g_sys_inited) {
			SAMPLE_COMM_SYS_Exit();
			g_sys_inited = CVI_FALSE;
		}
		return s32Ret;
	}
	g_vi_inited = CVI_TRUE;

	vpss_try_destroy(ZONHOR_MMF_GRP_CAM);
	vpss_try_destroy(ZONHOR_MMF_GRP_CSC);
	vpss_try_destroy(ZONHOR_MMF_GRP_CAM_CSC);

	/* ---- VPSS0: dual chn — cam RGB + ROT NV21 ---- */
	memset(&stVpssGrpAttr, 0, sizeof(stVpssGrpAttr));
	memset(astVpssChnAttr, 0, sizeof(astVpssChnAttr));
	memset(abChnEnable, 0, sizeof(abChnEnable));

	stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
	stVpssGrpAttr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
	stVpssGrpAttr.u32MaxW = g_stSensorSize.u32Width;
	stVpssGrpAttr.u32MaxH = g_stSensorSize.u32Height;
	stVpssGrpAttr.u8VpssDev = 0;

	/* chn0: Camera NV21 + GDC ROT (portrait user coords) */
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].u32Width = cam_pre_w;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].u32Height = cam_pre_h;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].enVideoFormat = VIDEO_FORMAT_LINEAR;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].enPixelFormat = SAMPLE_PIXEL_FORMAT;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].stFrameRate.s32SrcFrameRate = g_cam_fps;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].stFrameRate.s32DstFrameRate = g_cam_fps;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].u32Depth = 0; /* bound → VPSS2, no direct get */
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].bMirror = g_mirror;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].bFlip = g_flip;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].stAspectRatio.enMode = ASPECT_RATIO_AUTO;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].stAspectRatio.bEnableBgColor = CVI_TRUE;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
	astVpssChnAttr[ZONHOR_MMF_CHN_CAM].stNormalize.bEnable = CVI_FALSE;
	abChnEnable[ZONHOR_MMF_CHN_CAM] = CVI_TRUE;

	/* chn1: scale + ROT90 NV21 (GDC) */
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].u32Width = ZONHOR_MMF_PRE_W;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].u32Height = ZONHOR_MMF_PRE_H;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].enVideoFormat = VIDEO_FORMAT_LINEAR;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].enPixelFormat = SAMPLE_PIXEL_FORMAT;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].stFrameRate.s32SrcFrameRate = -1;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].stFrameRate.s32DstFrameRate = -1;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].u32Depth = 1;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].bMirror = g_mirror;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].bFlip = g_flip;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].stAspectRatio.enMode = ASPECT_RATIO_AUTO;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].stAspectRatio.bEnableBgColor = CVI_TRUE;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
	astVpssChnAttr[ZONHOR_MMF_CHN_ROT].stNormalize.bEnable = CVI_FALSE;
	abChnEnable[ZONHOR_MMF_CHN_ROT] = CVI_TRUE;

	memcpy(g_vpss0_chn_enabled, abChnEnable, sizeof(g_vpss0_chn_enabled));
	s32Ret = SAMPLE_COMM_VPSS_Init(ZONHOR_MMF_GRP_CAM, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss grp0 init failed: 0x%x", s32Ret);
		goto fail;
	}
	g_vpss0_started = CVI_TRUE;

	s32Ret = SAMPLE_COMM_VPSS_Start(ZONHOR_MMF_GRP_CAM, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss grp0 start failed: 0x%x", s32Ret);
		goto fail;
	}

	s32Ret = CVI_VPSS_SetChnRotation(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM,
					cam_rotation(g_cam_w, g_cam_h));
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss chn0 SetChnRotation failed: 0x%x", s32Ret);
		goto fail;
	}

	s32Ret = CVI_VPSS_SetChnRotation(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_ROT, ROTATION_90);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss chn1 SetChnRotation failed: 0x%x", s32Ret);
		goto fail;
	}

	/* ---- VPSS1: CSC NV21 → RGB888 (no rotation) ---- */
	memset(&stVpssGrpAttr, 0, sizeof(stVpssGrpAttr));
	memset(astVpssChnAttr, 0, sizeof(astVpssChnAttr));
	memset(abChnEnable, 0, sizeof(abChnEnable));

	stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
	stVpssGrpAttr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
	stVpssGrpAttr.u32MaxW = ZONHOR_MMF_DISP_W;
	stVpssGrpAttr.u32MaxH = ZONHOR_MMF_DISP_H;
	stVpssGrpAttr.u8VpssDev = 0;

	astVpssChnAttr[0].u32Width = ZONHOR_MMF_DISP_W;
	astVpssChnAttr[0].u32Height = ZONHOR_MMF_DISP_H;
	astVpssChnAttr[0].enVideoFormat = VIDEO_FORMAT_LINEAR;
	astVpssChnAttr[0].enPixelFormat = PIXEL_FORMAT_RGB_888;
	astVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
	astVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
	astVpssChnAttr[0].u32Depth = 1;
	astVpssChnAttr[0].bMirror = CVI_FALSE;
	astVpssChnAttr[0].bFlip = CVI_FALSE;
	astVpssChnAttr[0].stAspectRatio.enMode = ASPECT_RATIO_NONE;
	astVpssChnAttr[0].stAspectRatio.bEnableBgColor = CVI_FALSE;
	astVpssChnAttr[0].stNormalize.bEnable = CVI_FALSE;
	abChnEnable[0] = CVI_TRUE;

	memcpy(g_vpss1_chn_enabled, abChnEnable, sizeof(g_vpss1_chn_enabled));
	s32Ret = SAMPLE_COMM_VPSS_Init(ZONHOR_MMF_GRP_CSC, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss grp1 init failed: 0x%x", s32Ret);
		goto fail;
	}
	g_vpss1_started = CVI_TRUE;

	s32Ret = SAMPLE_COMM_VPSS_Start(ZONHOR_MMF_GRP_CSC, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss grp1 start failed: 0x%x", s32Ret);
		goto fail;
	}

	/* ---- VPSS2: camera NV21(+ROT) → RGB888 ---- */
	memset(&stVpssGrpAttr, 0, sizeof(stVpssGrpAttr));
	memset(astVpssChnAttr, 0, sizeof(astVpssChnAttr));
	memset(abChnEnable, 0, sizeof(abChnEnable));

	stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
	stVpssGrpAttr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
	stVpssGrpAttr.u32MaxW = cam_post_w;
	stVpssGrpAttr.u32MaxH = cam_post_h;
	stVpssGrpAttr.u8VpssDev = 0;

	astVpssChnAttr[0].u32Width = g_cam_w;
	astVpssChnAttr[0].u32Height = g_cam_h;
	astVpssChnAttr[0].enVideoFormat = VIDEO_FORMAT_LINEAR;
	astVpssChnAttr[0].enPixelFormat = PIXEL_FORMAT_RGB_888;
	astVpssChnAttr[0].stFrameRate.s32SrcFrameRate = g_cam_fps;
	astVpssChnAttr[0].stFrameRate.s32DstFrameRate = g_cam_fps;
	astVpssChnAttr[0].u32Depth = 1;
	astVpssChnAttr[0].bMirror = CVI_FALSE;
	astVpssChnAttr[0].bFlip = CVI_FALSE;
	astVpssChnAttr[0].stAspectRatio.enMode = ASPECT_RATIO_NONE;
	astVpssChnAttr[0].stAspectRatio.bEnableBgColor = CVI_FALSE;
	astVpssChnAttr[0].stNormalize.bEnable = CVI_FALSE;
	abChnEnable[0] = CVI_TRUE;

	memcpy(g_vpss_cam_csc_chn_enabled, abChnEnable, sizeof(g_vpss_cam_csc_chn_enabled));
	s32Ret = SAMPLE_COMM_VPSS_Init(ZONHOR_MMF_GRP_CAM_CSC, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss cam_csc init failed: 0x%x", s32Ret);
		goto fail;
	}
	g_vpss_cam_csc_started = CVI_TRUE;

	s32Ret = SAMPLE_COMM_VPSS_Start(ZONHOR_MMF_GRP_CAM_CSC, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("vpss cam_csc start failed: 0x%x", s32Ret);
		goto fail;
	}

	s32Ret = SAMPLE_COMM_VI_Bind_VPSS(0, 0, ZONHOR_MMF_GRP_CAM);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("VI bind VPSS0 failed: 0x%x", s32Ret);
		goto fail;
	}
	g_vi_vpss_bound = CVI_TRUE;

	s32Ret = SAMPLE_COMM_VPSS_Bind_VPSS(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_ROT, ZONHOR_MMF_GRP_CSC);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("VPSS0 chn1 bind VPSS1 failed: 0x%x", s32Ret);
		goto fail;
	}
	g_vpss_vpss_bound = CVI_TRUE;

	s32Ret = SAMPLE_COMM_VPSS_Bind_VPSS(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM,
					    ZONHOR_MMF_GRP_CAM_CSC);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("VPSS0 chn0 bind cam_csc failed: 0x%x", s32Ret);
		goto fail;
	}
	g_vpss_cam_csc_bound = CVI_TRUE;

	ZLOGI("Pipeline ready: sensor %ux%u -> cam NV21 %ux%u rot=%d -> NV21 %ux%u -> RGB %ux%u + preview %dx%d -> CSC %dx%d",
	      g_stSensorSize.u32Width, g_stSensorSize.u32Height,
	      cam_pre_w, cam_pre_h, (int)cam_rot, cam_post_w, cam_post_h,
	      g_cam_w, g_cam_h, ZONHOR_MMF_PRE_W, ZONHOR_MMF_PRE_H,
	      ZONHOR_MMF_DISP_W, ZONHOR_MMF_DISP_H);
	return CVI_SUCCESS;

fail:
	ZONHOR_MMF_Deinit();
	return s32Ret;
}

CVI_S32 ZONHOR_MMF_SetCamSize(CVI_U32 w, CVI_U32 h, CVI_S32 fps)
{
	VPSS_CHN_ATTR_S attr;
	CVI_U32 pre_w, pre_h;
	CVI_S32 ret;

	if (!ZONHOR_MMF_IsInited())
		return CVI_FAILURE;
	if (w == 0 || h == 0)
		return CVI_FAILURE;
	if (fps == 0)
		fps = g_cam_fps;

	cam_nv21_pre_rot_size(w, h, &pre_w, &pre_h);

	CVI_VPSS_DisableChn(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM);
	memset(&attr, 0, sizeof(attr));
	attr.u32Width = pre_w;
	attr.u32Height = pre_h;
	attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
	attr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
	attr.stFrameRate.s32SrcFrameRate = fps;
	attr.stFrameRate.s32DstFrameRate = fps;
	attr.u32Depth = 0;
	attr.bMirror = g_mirror;
	attr.bFlip = g_flip;
	attr.stAspectRatio.enMode = ASPECT_RATIO_AUTO;
	attr.stAspectRatio.bEnableBgColor = CVI_TRUE;
	attr.stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
	ret = CVI_VPSS_SetChnAttr(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM, &attr);
	if (ret != CVI_SUCCESS) {
		ZLOGE("SetCamSize cam NV21 SetChnAttr failed: 0x%x", ret);
		CVI_VPSS_EnableChn(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM);
		return ret;
	}
	ret = CVI_VPSS_EnableChn(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM);
	if (ret != CVI_SUCCESS)
		return ret;
	ret = CVI_VPSS_SetChnRotation(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM,
				      cam_rotation(w, h));
	if (ret != CVI_SUCCESS) {
		ZLOGE("SetCamSize cam SetChnRotation failed: 0x%x", ret);
		return ret;
	}

	CVI_VPSS_DisableChn(ZONHOR_MMF_GRP_CAM_CSC, 0);
	memset(&attr, 0, sizeof(attr));
	attr.u32Width = w;
	attr.u32Height = h;
	attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
	attr.enPixelFormat = PIXEL_FORMAT_RGB_888;
	attr.stFrameRate.s32SrcFrameRate = fps;
	attr.stFrameRate.s32DstFrameRate = fps;
	attr.u32Depth = 1;
	attr.bMirror = CVI_FALSE;
	attr.bFlip = CVI_FALSE;
	attr.stAspectRatio.enMode = ASPECT_RATIO_NONE;
	attr.stAspectRatio.bEnableBgColor = CVI_FALSE;
	ret = CVI_VPSS_SetChnAttr(ZONHOR_MMF_GRP_CAM_CSC, 0, &attr);
	if (ret != CVI_SUCCESS) {
		ZLOGE("SetCamSize cam CSC SetChnAttr failed: 0x%x", ret);
		CVI_VPSS_EnableChn(ZONHOR_MMF_GRP_CAM_CSC, 0);
		return ret;
	}
	ret = CVI_VPSS_EnableChn(ZONHOR_MMF_GRP_CAM_CSC, 0);
	if (ret == CVI_SUCCESS) {
		g_cam_w = w;
		g_cam_h = h;
		g_cam_fps = fps;
	}
	return ret;
}

CVI_S32 ZONHOR_MMF_SetMirrorFlip(CVI_BOOL mirror, CVI_BOOL flip)
{
	VPSS_CHN_ATTR_S attr;
	CVI_S32 ret;

	if (!ZONHOR_MMF_IsInited())
		return CVI_FAILURE;

	g_mirror = mirror;
	g_flip = flip;

	ret = CVI_VPSS_GetChnAttr(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM, &attr);
	if (ret == CVI_SUCCESS) {
		attr.bMirror = mirror;
		attr.bFlip = flip;
		CVI_VPSS_SetChnAttr(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_CAM, &attr);
	}

	ret = CVI_VPSS_GetChnAttr(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_ROT, &attr);
	if (ret == CVI_SUCCESS) {
		attr.bMirror = mirror;
		attr.bFlip = flip;
		ret = CVI_VPSS_SetChnAttr(ZONHOR_MMF_GRP_CAM, ZONHOR_MMF_CHN_ROT, &attr);
	}
	return ret;
}

CVI_S32 ZONHOR_MMF_CamGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
	if (!frame || !ZONHOR_MMF_IsInited())
		return CVI_FAILURE;
	return CVI_VPSS_GetChnFrame(ZONHOR_MMF_GRP_CAM_CSC, 0, frame, timeout_ms);
}

CVI_S32 ZONHOR_MMF_CamReleaseFrame(VIDEO_FRAME_INFO_S *frame)
{
	if (!frame)
		return CVI_FAILURE;
	return CVI_VPSS_ReleaseChnFrame(ZONHOR_MMF_GRP_CAM_CSC, 0, frame);
}

CVI_S32 ZONHOR_MMF_PreviewGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
	if (!frame || !ZONHOR_MMF_IsInited())
		return CVI_FAILURE;
	return CVI_VPSS_GetChnFrame(ZONHOR_MMF_GRP_CSC, 0, frame, timeout_ms);
}

CVI_S32 ZONHOR_MMF_PreviewReleaseFrame(VIDEO_FRAME_INFO_S *frame)
{
	if (!frame)
		return CVI_FAILURE;
	return CVI_VPSS_ReleaseChnFrame(ZONHOR_MMF_GRP_CSC, 0, frame);
}

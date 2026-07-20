/**
 * Profile-driven Zonhor media graph runtime.
 *
 * Topology (see 000_zonhor_mmf_camera_refactor_plan.md):
 *   VI -> Group0(Dev0) ROT90 YUV
 *       -> Group2(Dev1) ChA display RGB / ChB main RGB / ChC half YUV
 *           -> Group3(Dev1) reserved
 *   Group1(Dev0) MEM create-only
 */

#include "zonhor_graph_runtime.h"
#include "zonhor_sensor_mode.h"

#include "cvi_buffer.h"
#include "cvi_sys.h"
#include "cvi_vb.h"
#include "cvi_vpss.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZLOGI(fmt, ...) printf("[zonhor_graph] " fmt "\n", ##__VA_ARGS__)
#define ZLOGE(fmt, ...) printf("[zonhor_graph] ERROR: " fmt "\n", ##__VA_ARGS__)

static zonhor_graph_runtime_t g_rt;
static CVI_BOOL g_open;
static char g_sensor_ini_path[256];

static PIXEL_FORMAT_E to_cvi_fmt(z_pixel_format_e fmt)
{
	return (fmt == Z_PIXEL_RGB888) ? PIXEL_FORMAT_RGB_888 : SAMPLE_PIXEL_FORMAT;
}

static ROTATION_E to_cvi_rot(z_rotation_e rot)
{
	switch (rot) {
	case Z_ROTATION_90:
		return ROTATION_90;
	case Z_ROTATION_180:
		return ROTATION_180;
	case Z_ROTATION_270:
		return ROTATION_270;
	default:
		return ROTATION_0;
	}
}

static CVI_U32 vb_blk(CVI_U32 w, CVI_U32 h, PIXEL_FORMAT_E fmt)
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

/**
 * Resolve sensor_cfg.ini before ParseIni.
 * Priority: env MAIX_SENSOR_CFG_INI > explicit/SetSensorIniPath >
 * size-based default (1080p envelope → 2x2 bin, else 5MP crop).
 */
static void resolve_sensor_ini(CVI_U32 cam_w, CVI_U32 cam_h, const char *explicit_ini)
{
	const char *env = getenv("MAIX_SENSOR_CFG_INI");
	const char *path = NULL;
	const char *via = NULL;
	char auto_buf[sizeof(g_sensor_ini_path)];

	if (env && env[0]) {
		path = env;
		via = "env";
	} else if (explicit_ini && explicit_ini[0]) {
		path = explicit_ini;
		via = "explicit";
	} else if (g_sensor_ini_path[0]) {
		path = g_sensor_ini_path;
		via = "pending";
	} else {
		path = zonhor_sns_ini_for_size(cam_w, cam_h);
		via = "auto";
	}

	if (path != g_sensor_ini_path) {
		snprintf(auto_buf, sizeof(auto_buf), "%s", path);
		snprintf(g_sensor_ini_path, sizeof(g_sensor_ini_path), "%s", auto_buf);
	}

	if (SAMPLE_COMM_VI_SetIniPath(g_sensor_ini_path) != CVI_SUCCESS) {
		ZLOGE("SAMPLE_COMM_VI_SetIniPath(%s) failed", g_sensor_ini_path);
		return;
	}
	ZLOGI("sensor ini (%s) cam=%ux%u → %s", via, cam_w, cam_h, g_sensor_ini_path);
}

/**
 * GDC pre-rotation channel size so post-rot buffer width is 64-aligned.
 * Input sensor (W,H); after ROT90 logical (H,W), buffer (align(H), W).
 * Pre-rot attr = (W, align(H)).
 */
static void gdc_pre_rot_size(uint32_t sensor_w, uint32_t sensor_h,
			     CVI_U32 *pre_w, CVI_U32 *pre_h)
{
	*pre_w = sensor_w;
	*pre_h = z_align_up_64(sensor_h);
}

static void get_vpss_output_size(const z_channel_profile_t *ch,
				 CVI_U32 sensor_w, CVI_U32 sensor_h,
				 CVI_U32 *out_w, CVI_U32 *out_h)
{
	if (ch->use_pre_rotation_attr_size && ch->rotation != Z_ROTATION_0) {
		gdc_pre_rot_size(sensor_w, sensor_h, out_w, out_h);
		return;
	}

	*out_w = ch->extent.logical_width;
	*out_h = ch->extent.logical_height;
}

static void log_vpss_chn_cfg(const char *group_name, const z_channel_profile_t *ch,
			     CVI_U32 attr_w, CVI_U32 attr_h)
{
	ZLOGI("%s ch%u %s attr=%ux%u logical=%ux%u storage=%ux%u valid=(%u,%u %ux%u) rot=%d pre_rot_attr=%d",
	      group_name, ch->channel_id, ch->name,
	      attr_w, attr_h,
	      ch->extent.logical_width, ch->extent.logical_height,
	      ch->extent.buffer_width, ch->extent.buffer_height,
	      ch->extent.valid_x, ch->extent.valid_y,
	      ch->extent.valid_width, ch->extent.valid_height,
	      (int)ch->rotation, (int)ch->use_pre_rotation_attr_size);
}

static void fill_chn_attr(VPSS_CHN_ATTR_S *attr, const z_channel_profile_t *ch,
			  CVI_S32 fps, CVI_BOOL mirror, CVI_BOOL flip,
			  CVI_U32 depth, CVI_U32 sensor_w, CVI_U32 sensor_h)
{
	memset(attr, 0, sizeof(*attr));
	get_vpss_output_size(ch, sensor_w, sensor_h, &attr->u32Width, &attr->u32Height);
	attr->enVideoFormat = VIDEO_FORMAT_LINEAR;
	attr->enPixelFormat = to_cvi_fmt(ch->pixel_format);
	attr->stFrameRate.s32SrcFrameRate = fps;
	attr->stFrameRate.s32DstFrameRate = fps;
	attr->u32Depth = depth;
	attr->bMirror = mirror;
	attr->bFlip = flip;
	/*
	 * ASPECT_RATIO_AUTO is only for Group0 GDC pre-rot letterbox so the
	 * post-rot storage width stays 64-aligned without stretching content.
	 * Ordinary Group2/3 channels already use logical SetChnAttr sizes;
	 * do NOT reintroduce buffer-sized letterbox here.
	 */
	if (ch->use_pre_rotation_attr_size && ch->rotation != Z_ROTATION_0) {
		attr->stAspectRatio.enMode = ASPECT_RATIO_AUTO;
		attr->stAspectRatio.bEnableBgColor = CVI_TRUE;
		attr->stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
	} else {
		attr->stAspectRatio.enMode = ASPECT_RATIO_NONE;
		attr->stAspectRatio.bEnableBgColor = CVI_FALSE;
	}
	attr->stNormalize.bEnable = CVI_FALSE;
}

/**
 * Crop Group2 input to the post-rotation logical picture, dropping the
 * GDC letterbox columns that exist only in the 64-aligned storage width
 * (e.g. keep logical 1080x1920 inside buffer 1088x1920).
 *
 * Contract: GrpAttr.u32MaxW/H must already equal buffer_*; this crop only
 * selects the valid_* rectangle — it must not fight a MaxW larger than buffer.
 */
static CVI_S32 apply_rotated_input_crop(VPSS_GRP gid, const z_frame_extent_t *ext)
{
	VPSS_CROP_INFO_S crop;

	if (!z_extent_needs_crop(ext)) {
		ZLOGI("Group%u input crop skip (valid fills buffer %ux%u)",
		      (unsigned)gid,
		      ext ? ext->buffer_width : 0u,
		      ext ? ext->buffer_height : 0u);
		return CVI_SUCCESS;
	}

	memset(&crop, 0, sizeof(crop));
	crop.bEnable = CVI_TRUE;
	crop.enCropCoordinate = VPSS_CROP_ABS_COOR;
	crop.stCropRect.s32X = (CVI_S32)ext->valid_x;
	crop.stCropRect.s32Y = (CVI_S32)ext->valid_y;
	crop.stCropRect.u32Width = ext->valid_width;
	crop.stCropRect.u32Height = ext->valid_height;

	ZLOGI("Group%u input crop enable=(%d,%d %ux%u) from storage=%ux%u logical=%ux%u",
	      (unsigned)gid,
	      crop.stCropRect.s32X, crop.stCropRect.s32Y,
	      crop.stCropRect.u32Width, crop.stCropRect.u32Height,
	      ext->buffer_width, ext->buffer_height,
	      ext->logical_width, ext->logical_height);
	return CVI_VPSS_SetGrpCrop(gid, &crop);
}

static CVI_S32 create_vpss_group(const z_vpss_group_profile_t *grp,
				 CVI_S32 fps, CVI_BOOL mirror, CVI_BOOL flip,
				 CVI_U32 sensor_w, CVI_U32 sensor_h,
				 CVI_BOOL chn_enabled_out[VPSS_MAX_PHY_CHN_NUM])
{
	VPSS_GRP_ATTR_S stVpssGrpAttr;
	VPSS_CHN_ATTR_S attr;
	CVI_S32 s32Ret;
	uint32_t c;
	VPSS_GRP gid = (VPSS_GRP)grp->group_id;

	memset(&stVpssGrpAttr, 0, sizeof(stVpssGrpAttr));
	memset(chn_enabled_out, 0, sizeof(CVI_BOOL) * VPSS_MAX_PHY_CHN_NUM);

	stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
	stVpssGrpAttr.enPixelFormat = SAMPLE_PIXEL_FORMAT;
	stVpssGrpAttr.u32MaxW = grp->input_extent.buffer_width;
	stVpssGrpAttr.u32MaxH = grp->input_extent.buffer_height;
	if (grp->input_type == Z_VPSS_INPUT_VI || grp->input_type == Z_VPSS_INPUT_MEM) {
		stVpssGrpAttr.u32MaxW = sensor_w;
		stVpssGrpAttr.u32MaxH = sensor_h;
	}
	/*
	 * VPSS←VPSS groups must use the bound input buffer size as MaxW/H.
	 * Do NOT lift to landscape sensor_w/h: after G0 ROT90 the input is
	 * portrait (e.g. 1088x1920). Using MaxW=1920 makes CSC read past each
	 * 1088-wide NV21 row → zipper / neon-block garbage on HW RGB paths.
	 * Board-proven 2026-07-20 (camera_capture_debug_demo padding validate).
	 */
	if (grp->input_type == Z_VPSS_INPUT_VPSS) {
		stVpssGrpAttr.u32MaxW = grp->input_extent.buffer_width;
		stVpssGrpAttr.u32MaxH = grp->input_extent.buffer_height;
		ZLOGI("%s GrpAttr MaxW/H=%ux%u (bound input buffer)",
		      grp->node_name, stVpssGrpAttr.u32MaxW, stVpssGrpAttr.u32MaxH);
	}
	stVpssGrpAttr.u8VpssDev = (CVI_U8)grp->device_id;

	vpss_try_destroy(gid);

	s32Ret = CVI_VPSS_CreateGrp(gid, &stVpssGrpAttr);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("%s CreateGrp failed: 0x%x", grp->node_name, s32Ret);
		return s32Ret;
	}
	(void)CVI_VPSS_ResetGrp(gid);

	for (c = 0; c < grp->channel_count && c < VPSS_MAX_PHY_CHN_NUM; ++c) {
		const z_channel_profile_t *ch = &grp->channels[c];
		CVI_U32 depth = 1;
		CVI_BOOL should_enable;

		if (grp->group_id == 0)
			depth = 1; /* allow GetChnFrame for offset/layout diagnostics */
		if (grp->group_id == 2 && ch->channel_id == 2)
			depth = 0;

		fill_chn_attr(&attr, ch, fps, mirror, flip, depth, sensor_w, sensor_h);
		s32Ret = CVI_VPSS_SetChnAttr(gid, (VPSS_CHN)ch->channel_id, &attr);
		if (s32Ret != CVI_SUCCESS) {
			ZLOGE("%s SetChnAttr ch%u failed: 0x%x",
			      grp->node_name, ch->channel_id, s32Ret);
			return s32Ret;
		}
		log_vpss_chn_cfg(grp->node_name, ch, attr.u32Width, attr.u32Height);

		if (ch->rotation != Z_ROTATION_0) {
			s32Ret = CVI_VPSS_SetChnRotation(gid, (VPSS_CHN)ch->channel_id,
							to_cvi_rot(ch->rotation));
			if (s32Ret != CVI_SUCCESS) {
				ZLOGE("%s SetChnRotation ch%u failed: 0x%x",
				      grp->node_name, ch->channel_id, s32Ret);
				return s32Ret;
			}
		}

		should_enable = ch->enabled_by_default && !ch->create_only && !grp->create_only;
		if (should_enable) {
			s32Ret = CVI_VPSS_EnableChn(gid, (VPSS_CHN)ch->channel_id);
			if (s32Ret != CVI_SUCCESS) {
				ZLOGE("%s EnableChn %u failed: 0x%x",
				      grp->node_name, ch->channel_id, s32Ret);
				return s32Ret;
			}
			chn_enabled_out[ch->channel_id] = CVI_TRUE;
		}
	}

	ZLOGI("%s created grp=%u dev=%u (channels configured; start deferred)",
	      grp->node_name, grp->group_id, grp->device_id);
	return CVI_SUCCESS;
}

static CVI_S32 build_vb_and_sys(const zonhor_camera_graph_profile_t *p)
{
	VB_CONFIG_S stVbConf;
	CVI_U32 vi_blk, g0_blk, disp_blk, main_blk, half_blk, g1_blk;
	CVI_S32 s32Ret;
	CVI_U32 main_pixels;

	memset(&stVbConf, 0, sizeof(stVbConf));

	vi_blk = vb_blk(p->sensor.sensor_w, p->sensor.sensor_h, SAMPLE_PIXEL_FORMAT);
	{
		CVI_U32 rot_blk = vb_blk(p->rotated_main.buffer_width,
					 p->rotated_main.buffer_height,
					 SAMPLE_PIXEL_FORMAT);
		if (rot_blk > vi_blk)
			vi_blk = rot_blk;
	}
	g0_blk = vb_blk(p->rotated_main.buffer_width, p->rotated_main.buffer_height,
			SAMPLE_PIXEL_FORMAT);
	disp_blk = vb_blk(p->group2.channels[0].extent.buffer_width,
			  p->group2.channels[0].extent.buffer_height,
			  PIXEL_FORMAT_RGB_888);
	main_blk = vb_blk(p->group2.channels[1].extent.buffer_width,
			  p->group2.channels[1].extent.buffer_height,
			  PIXEL_FORMAT_RGB_888);
	half_blk = vb_blk(p->group2.channels[2].extent.buffer_width,
			  p->group2.channels[2].extent.buffer_height,
			  SAMPLE_PIXEL_FORMAT);
	g1_blk = vb_blk(p->group1.channels[0].extent.buffer_width,
			p->group1.channels[0].extent.buffer_height,
			SAMPLE_PIXEL_FORMAT);

	main_pixels = p->group2.channels[1].extent.logical_width *
		      p->group2.channels[1].extent.logical_height;

	stVbConf.u32MaxPoolCnt = 6;
	stVbConf.astCommPool[0].u32BlkSize = vi_blk;
	stVbConf.astCommPool[0].u32BlkCnt = 4;
	stVbConf.astCommPool[0].enRemapMode = VB_REMAP_MODE_CACHED;
	stVbConf.astCommPool[1].u32BlkSize = g0_blk;
	stVbConf.astCommPool[1].u32BlkCnt = 5;
	stVbConf.astCommPool[1].enRemapMode = VB_REMAP_MODE_CACHED;
	stVbConf.astCommPool[2].u32BlkSize = main_blk;
	stVbConf.astCommPool[2].u32BlkCnt = (main_pixels > (1920u * 1080u)) ? 2 : 3;
	stVbConf.astCommPool[2].enRemapMode = VB_REMAP_MODE_CACHED;
	stVbConf.astCommPool[3].u32BlkSize = disp_blk;
	stVbConf.astCommPool[3].u32BlkCnt = 3;
	stVbConf.astCommPool[3].enRemapMode = VB_REMAP_MODE_CACHED;
	/* G2-ChC (bind) + G3-Ch0 (GetFrame) share this size; keep headroom. */
	stVbConf.astCommPool[4].u32BlkSize = half_blk;
	stVbConf.astCommPool[4].u32BlkCnt = 6;
	stVbConf.astCommPool[4].enRemapMode = VB_REMAP_MODE_CACHED;
	stVbConf.astCommPool[5].u32BlkSize = g1_blk;
	stVbConf.astCommPool[5].u32BlkCnt = 2;
	stVbConf.astCommPool[5].enRemapMode = VB_REMAP_MODE_CACHED;

	ZLOGI("VB vi=%u g0=%u main=%u disp=%u half=%u g1=%u",
	      vi_blk, g0_blk, main_blk, disp_blk, half_blk, g1_blk);

	s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("SYS_Init failed: 0x%x", s32Ret);
		return s32Ret;
	}
	return CVI_SUCCESS;
}

static void set_dual_vi_vpss_mode(void)
{
	VI_VPSS_MODE_S stViVpssMode;

	CVI_SYS_SetVPSSMode(VPSS_MODE_DUAL);
	memset(&stViVpssMode, 0, sizeof(stViVpssMode));
	/* Pipe0 unused offline; pipe1 / primary: VI ONLINE + VPSS OFFLINE */
	stViVpssMode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
	stViVpssMode.aenMode[1] = VI_ONLINE_VPSS_OFFLINE;
	CVI_SYS_SetVIVPSSMode(&stViVpssMode);
	ZLOGI("VPSS Dual + VI_ONLINE_VPSS_OFFLINE");
}

static void map_output_to_grp_chn(z_camera_output_id_t id, uint32_t *gid, uint32_t *cid)
{
	z_camera_output_desc_t desc;

	*gid = 0;
	*cid = 0;
	if (zonhor_profile_get_output(&g_rt.profile, id, &desc) == 0) {
		*gid = desc.group_id;
		*cid = desc.channel_id;
	}
}

void zonhor_graph_default_params(zonhor_graph_open_params_t *p)
{
	if (!p)
		return;
	memset(p, 0, sizeof(*p));
	p->cam_w = ZONHOR_SNS_DEFAULT_USER_W;
	p->cam_h = ZONHOR_SNS_DEFAULT_USER_H;
	p->cam_fps = 30;
	p->mirror = CVI_FALSE;
	p->flip = CVI_FALSE;
	/* NULL → size-based auto (1080p envelope → imx678_1080p_bin). */
	p->sensor_ini = NULL;
}

zonhor_graph_runtime_t *zonhor_graph_runtime_get(void)
{
	return &g_rt;
}

void zonhor_graph_set_sensor_ini_path(const char *ini_path)
{
	if (!ini_path || ini_path[0] == '\0') {
		g_sensor_ini_path[0] = '\0';
		return;
	}
	snprintf(g_sensor_ini_path, sizeof(g_sensor_ini_path), "%s", ini_path);
}

const char *zonhor_graph_get_sensor_ini_path(void)
{
	const char *env = getenv("MAIX_SENSOR_CFG_INI");

	if (env && env[0])
		return env;
	return g_sensor_ini_path;
}

CVI_BOOL zonhor_graph_is_open(void)
{
	return g_open;
}

const zonhor_camera_graph_profile_t *zonhor_graph_profile(void)
{
	return g_open ? &g_rt.profile : NULL;
}

void zonhor_graph_get_sensor_size(CVI_U32 *w, CVI_U32 *h)
{
	if (w)
		*w = g_rt.sensor_size.u32Width;
	if (h)
		*h = g_rt.sensor_size.u32Height;
}

void zonhor_graph_get_user_size(CVI_U32 *w, CVI_U32 *h)
{
	if (w)
		*w = g_rt.profile.sensor.user_w;
	if (h)
		*h = g_rt.profile.sensor.user_h;
}

void zonhor_graph_close(void)
{
	if (!g_open && !g_rt.sys_inited)
		return;

	/* 1) Stop upstream → downstream consumers (plan §9.2). */
	if (g_rt.group_state[0] != Z_NODE_STATE_NONE) {
		CVI_S32 j;
		for (j = 0; j < VPSS_MAX_PHY_CHN_NUM; j++) {
			if (g_rt.chn_enabled[0][j])
				CVI_VPSS_DisableChn(0, j);
		}
		CVI_VPSS_StopGrp(0);
		g_rt.group_state[0] = Z_NODE_STATE_CREATED;
	}
	if (g_rt.group_state[2] != Z_NODE_STATE_NONE) {
		CVI_S32 j;
		for (j = 0; j < VPSS_MAX_PHY_CHN_NUM; j++) {
			if (g_rt.chn_enabled[2][j])
				CVI_VPSS_DisableChn(2, j);
		}
		CVI_VPSS_StopGrp(2);
		g_rt.group_state[2] = Z_NODE_STATE_CREATED;
	}
	if (g_rt.group_state[3] != Z_NODE_STATE_NONE) {
		CVI_S32 j;
		for (j = 0; j < VPSS_MAX_PHY_CHN_NUM; j++) {
			if (g_rt.chn_enabled[3][j])
				CVI_VPSS_DisableChn(3, j);
		}
		CVI_VPSS_StopGrp(3);
		g_rt.group_state[3] = Z_NODE_STATE_CREATED;
	}

	/* 2) Unbind. */
	if (g_rt.g2_g3_bound) {
		SAMPLE_COMM_VPSS_UnBind_VPSS(2, 2, 3);
		g_rt.g2_g3_bound = CVI_FALSE;
	}
	if (g_rt.g0_g2_bound) {
		SAMPLE_COMM_VPSS_UnBind_VPSS(0, 0, 2);
		g_rt.g0_g2_bound = CVI_FALSE;
	}
	if (g_rt.vi_vpss_bound) {
		SAMPLE_COMM_VI_UnBind_VPSS(0, 0, 0);
		g_rt.vi_vpss_bound = CVI_FALSE;
	}

	/* 3) Destroy groups (G0/G2/G3 + create-only G1). */
	vpss_try_destroy(0);
	vpss_try_destroy(2);
	vpss_try_destroy(3);
	vpss_try_destroy(1);
	g_rt.group_state[0] = Z_NODE_STATE_NONE;
	g_rt.group_state[1] = Z_NODE_STATE_NONE;
	g_rt.group_state[2] = Z_NODE_STATE_NONE;
	g_rt.group_state[3] = Z_NODE_STATE_NONE;
	memset(g_rt.chn_enabled, 0, sizeof(g_rt.chn_enabled));
	memset(g_rt.channel_state, 0, sizeof(g_rt.channel_state));

	/* 4) Destroy VI + SYS. */
	if (g_rt.vi_inited) {
		SAMPLE_COMM_VI_DestroyIsp(&g_rt.vi_config);
		SAMPLE_COMM_VI_DestroyVi(&g_rt.vi_config);
		g_rt.vi_inited = CVI_FALSE;
	}
	if (g_rt.sys_inited) {
		SAMPLE_COMM_SYS_Exit();
		g_rt.sys_inited = CVI_FALSE;
	}

	g_open = CVI_FALSE;
	ZLOGI("graph closed");
}

CVI_S32 zonhor_graph_open(const zonhor_graph_open_params_t *params)
{
	zonhor_graph_open_params_t local;
	SAMPLE_INI_CFG_S stIniCfg;
	SAMPLE_VI_CONFIG_S stViConfig;
	z_sensor_caps_t caps;
	MMF_VERSION_S stVersion;
	LOG_LEVEL_CONF_S log_conf;
	CVI_S32 s32Ret;

	if (g_open) {
		ZLOGI("already open");
		return CVI_SUCCESS;
	}

	if (params)
		local = *params;
	else
		zonhor_graph_default_params(&local);

	if (local.cam_w == 0)
		local.cam_w = ZONHOR_SNS_DEFAULT_USER_W;
	if (local.cam_h == 0)
		local.cam_h = ZONHOR_SNS_DEFAULT_USER_H;
	if (local.cam_fps == 0)
		local.cam_fps = 30;

	memset(&g_rt, 0, sizeof(g_rt));
	g_rt.mirror = local.mirror;
	g_rt.flip = local.flip;

	CVI_SYS_GetVersion(&stVersion);
	ZLOGI("MMF Version:%s", stVersion.version);

	log_conf.enModId = CVI_ID_LOG;
	log_conf.s32Level = CVI_DBG_INFO;
	CVI_LOG_SetLevelConf(&log_conf);

	/* Prefer 2x2 binning for ≤1080p (1920x1080 or 1080x1920). */
	resolve_sensor_ini(local.cam_w, local.cam_h, local.sensor_ini);

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
		g_rt.sensor_size = stModeInfo.stSize;
		ZLOGI("Sensor mode=%s size=%ux%u",
		      stModeInfo.pszModeName,
		      stModeInfo.stSize.u32Width, stModeInfo.stSize.u32Height);
	}

	memset(&caps, 0, sizeof(caps));
	caps.sensor_w = g_rt.sensor_size.u32Width;
	caps.sensor_h = g_rt.sensor_size.u32Height;
	caps.user_w = local.cam_w;
	caps.user_h = local.cam_h;
	caps.fps = local.cam_fps;

	if (zonhor_build_camera_graph_profile(&caps, &g_rt.profile) != 0) {
		ZLOGE("build profile failed");
		return CVI_FAILURE;
	}
	zonhor_dump_camera_graph_profile(&g_rt.profile);

	CVI_VI_SetDevNum(stIniCfg.devNum);
	s32Ret = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, &stViConfig);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("IniToViCfg fail: 0x%x", s32Ret);
		return s32Ret;
	}
	memcpy(&g_rt.vi_config, &stViConfig, sizeof(stViConfig));

	/*
	 * After kill -9, kernel may still hold VPSS groups referencing old VB.
	 * Tear them down before VB/SYS re-init so Dev1 jobs do not StartFail.
	 */
	{
		CVI_S32 gi;
		for (gi = 0; gi < 4; ++gi)
			vpss_try_destroy((VPSS_GRP)gi);
	}

	s32Ret = build_vb_and_sys(&g_rt.profile);
	if (s32Ret != CVI_SUCCESS)
		return s32Ret;
	g_rt.sys_inited = CVI_TRUE;

	s32Ret = SAMPLE_PLAT_VI_INIT(&stViConfig);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("VI init failed: 0x%x", s32Ret);
		zonhor_graph_close();
		return s32Ret;
	}
	g_rt.vi_inited = CVI_TRUE;

	set_dual_vi_vpss_mode();

	/* Create groups: G0, G1(create-only no start), G2, G3. */
	s32Ret = create_vpss_group(&g_rt.profile.group0, local.cam_fps,
				   local.mirror, local.flip,
				   caps.sensor_w, caps.sensor_h,
				   g_rt.chn_enabled[0]);
	if (s32Ret != CVI_SUCCESS)
		goto fail;
	g_rt.group_state[0] = Z_NODE_STATE_CREATED;
	g_rt.channel_state[0][0] = Z_NODE_STATE_CREATED;

	s32Ret = create_vpss_group(&g_rt.profile.group1, local.cam_fps,
				   CVI_FALSE, CVI_FALSE,
				   caps.sensor_w, caps.sensor_h,
				   g_rt.chn_enabled[1]);
	if (s32Ret != CVI_SUCCESS)
		goto fail;
	g_rt.group_state[1] = Z_NODE_STATE_RESERVED;
	g_rt.channel_state[1][0] = Z_NODE_STATE_RESERVED;

	s32Ret = create_vpss_group(&g_rt.profile.group2, local.cam_fps,
				   CVI_FALSE, CVI_FALSE,
				   caps.sensor_w, caps.sensor_h,
				   g_rt.chn_enabled[2]);
	if (s32Ret != CVI_SUCCESS)
		goto fail;
	s32Ret = apply_rotated_input_crop(2, &g_rt.profile.rotated_main);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("Group2 SetGrpCrop failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.group_state[2] = Z_NODE_STATE_CREATED;
	g_rt.channel_state[2][0] = Z_NODE_STATE_CREATED;
	g_rt.channel_state[2][1] = Z_NODE_STATE_CREATED;
	g_rt.channel_state[2][2] = Z_NODE_STATE_CREATED;

	s32Ret = create_vpss_group(&g_rt.profile.group3, local.cam_fps,
				   CVI_FALSE, CVI_FALSE,
				   caps.sensor_w, caps.sensor_h,
				   g_rt.chn_enabled[3]);
	if (s32Ret != CVI_SUCCESS)
		goto fail;
	g_rt.group_state[3] = Z_NODE_STATE_CREATED;
	g_rt.channel_state[3][0] = Z_NODE_STATE_RESERVED;
	g_rt.channel_state[3][1] = Z_NODE_STATE_RESERVED;
	g_rt.channel_state[3][2] = Z_NODE_STATE_RESERVED;

	/* Bind: VI->G0, G0-Ch0->G2, G2-ChC->G3 */
	s32Ret = SAMPLE_COMM_VI_Bind_VPSS(0, 0, 0);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("VI bind G0 failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.vi_vpss_bound = CVI_TRUE;
	g_rt.group_state[0] = Z_NODE_STATE_BOUND;

	s32Ret = SAMPLE_COMM_VPSS_Bind_VPSS(0, 0, 2);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("G0 bind G2 failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.g0_g2_bound = CVI_TRUE;
	g_rt.group_state[2] = Z_NODE_STATE_BOUND;

#if 0 /* TEMP_LANDSCAPE_CSC_TEST: G2-Ch2 disabled, skip bind to G3 */
	ZLOGI("TEMP_LANDSCAPE_CSC_TEST: skip G2-ChC bind G3");
#else
	s32Ret = SAMPLE_COMM_VPSS_Bind_VPSS(2, 2, 3);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("G2-ChC bind G3 failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.g2_g3_bound = CVI_TRUE;
	g_rt.group_state[3] = Z_NODE_STATE_BOUND;
#endif

	/* Start downstream first: G3 -> G2 -> G0. Group1 remains create-only. */
	s32Ret = CVI_VPSS_StartGrp(3);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("G3 StartGrp failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.group_state[3] = Z_NODE_STATE_ENABLED;

	s32Ret = CVI_VPSS_StartGrp(2);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("G2 StartGrp failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.group_state[2] = Z_NODE_STATE_ENABLED;
	{
		uint32_t c;
		for (c = 0; c < g_rt.profile.group2.channel_count; ++c) {
			if (g_rt.chn_enabled[2][c])
				g_rt.channel_state[2][c] = Z_NODE_STATE_ENABLED;
		}
	}

	s32Ret = CVI_VPSS_StartGrp(0);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("G0 StartGrp failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.group_state[0] = Z_NODE_STATE_ENABLED;
	if (g_rt.chn_enabled[0][0])
		g_rt.channel_state[0][0] = Z_NODE_STATE_ENABLED;

	g_open = CVI_TRUE;
	ZLOGI("graph open ok: VI -> G0 -> G2 (disp/main/half) -> G3(reserved); G1 create-only");
	return CVI_SUCCESS;

fail:
	zonhor_graph_close();
	return s32Ret;
}

CVI_S32 zonhor_graph_get_output_desc(z_camera_output_id_t id, z_camera_output_desc_t *desc)
{
	if (!g_open)
		return CVI_FAILURE;
	return zonhor_profile_get_output(&g_rt.profile, id, desc) == 0 ? CVI_SUCCESS : CVI_FAILURE;
}

CVI_S32 zonhor_graph_enable_output(z_camera_output_id_t id)
{
	uint32_t gid, cid;
	CVI_S32 ret;

	if (!g_open)
		return CVI_FAILURE;
	map_output_to_grp_chn(id, &gid, &cid);
	if (gid == 1) {
		/* Group1 was create-only: start group + enable channel. */
		ret = CVI_VPSS_StartGrp((VPSS_GRP)gid);
		if (ret != CVI_SUCCESS)
			ZLOGE("enable USER_MEM StartGrp: 0x%x (may already be started)", ret);
	}
	ret = CVI_VPSS_EnableChn((VPSS_GRP)gid, (VPSS_CHN)cid);
	if (ret == CVI_SUCCESS) {
		g_rt.chn_enabled[gid][cid] = CVI_TRUE;
		g_rt.channel_state[gid][cid] = Z_NODE_STATE_ENABLED;
		g_rt.group_state[gid] = Z_NODE_STATE_ENABLED;
	}
	return ret;
}

CVI_S32 zonhor_graph_disable_output(z_camera_output_id_t id)
{
	uint32_t gid, cid;
	CVI_S32 ret;

	if (!g_open)
		return CVI_FAILURE;
	map_output_to_grp_chn(id, &gid, &cid);
	ret = CVI_VPSS_DisableChn((VPSS_GRP)gid, (VPSS_CHN)cid);
	if (ret == CVI_SUCCESS) {
		g_rt.chn_enabled[gid][cid] = CVI_FALSE;
		g_rt.channel_state[gid][cid] = Z_NODE_STATE_CREATED;
	}
	return ret;
}

CVI_S32 zonhor_graph_get_frame(z_camera_output_id_t id, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
	uint32_t gid, cid;

	if (!frame || !g_open)
		return CVI_FAILURE;
	map_output_to_grp_chn(id, &gid, &cid);
	return CVI_VPSS_GetChnFrame((VPSS_GRP)gid, (VPSS_CHN)cid, frame, timeout_ms);
}

CVI_S32 zonhor_graph_release_frame(z_camera_output_id_t id, VIDEO_FRAME_INFO_S *frame)
{
	uint32_t gid, cid;

	if (!frame)
		return CVI_FAILURE;
	map_output_to_grp_chn(id, &gid, &cid);
	return CVI_VPSS_ReleaseChnFrame((VPSS_GRP)gid, (VPSS_CHN)cid, frame);
}

CVI_S32 zonhor_graph_set_mirror_flip(CVI_BOOL mirror, CVI_BOOL flip)
{
	VPSS_CHN_ATTR_S attr;
	CVI_S32 ret;

	if (!g_open)
		return CVI_FAILURE;
	g_rt.mirror = mirror;
	g_rt.flip = flip;

	ret = CVI_VPSS_GetChnAttr(0, 0, &attr);
	if (ret == CVI_SUCCESS) {
		attr.bMirror = mirror;
		attr.bFlip = flip;
		ret = CVI_VPSS_SetChnAttr(0, 0, &attr);
	}
	return ret;
}

CVI_S32 zonhor_graph_set_user_size(CVI_U32 w, CVI_U32 h, CVI_S32 fps)
{
	VPSS_CHN_ATTR_S attr;
	z_frame_extent_t main_ext;
	CVI_S32 ret;
#if 0 /* TEMP_LANDSCAPE_CSC_TEST */
	const VPSS_CHN csc_chn = 0;
#else
	const VPSS_CHN csc_chn = 1;
#endif

	if (!g_open || w == 0 || h == 0)
		return CVI_FAILURE;
	if (fps == 0)
		fps = g_rt.profile.sensor.fps;

	z_frame_layout_calc(w, h, &main_ext);

	CVI_VPSS_DisableChn(2, csc_chn);
	memset(&attr, 0, sizeof(attr));
	attr.u32Width = main_ext.logical_width;
	attr.u32Height = main_ext.logical_height;
	attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
	attr.enPixelFormat = PIXEL_FORMAT_RGB_888;
	attr.stFrameRate.s32SrcFrameRate = fps;
	attr.stFrameRate.s32DstFrameRate = fps;
	attr.u32Depth = 1;
	attr.stAspectRatio.enMode = ASPECT_RATIO_NONE;
	ret = CVI_VPSS_SetChnAttr(2, csc_chn, &attr);
	if (ret != CVI_SUCCESS) {
		CVI_VPSS_EnableChn(2, csc_chn);
		return ret;
	}
	ret = CVI_VPSS_EnableChn(2, csc_chn);
	if (ret == CVI_SUCCESS) {
		g_rt.profile.sensor.user_w = w;
		g_rt.profile.sensor.user_h = h;
		g_rt.profile.sensor.fps = fps;
		g_rt.profile.group2.channels[csc_chn].extent = main_ext;
		g_rt.chn_enabled[2][csc_chn] = CVI_TRUE;
		ZLOGI("SetCamSize ch%u attr=%ux%u logical=%ux%u storage=%ux%u valid=(%u,%u %ux%u)",
		      (unsigned)csc_chn,
		      attr.u32Width, attr.u32Height,
		      main_ext.logical_width, main_ext.logical_height,
		      main_ext.buffer_width, main_ext.buffer_height,
		      main_ext.valid_x, main_ext.valid_y,
		      main_ext.valid_width, main_ext.valid_height);
	}
	return ret;
}

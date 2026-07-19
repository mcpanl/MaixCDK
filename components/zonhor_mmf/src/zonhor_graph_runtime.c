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

static void apply_ini_path_override(void)
{
	const char *path = getenv("MAIX_SENSOR_CFG_INI");

	if (!path || path[0] == '\0')
		path = g_sensor_ini_path[0] ? g_sensor_ini_path : NULL;
	if (!path || path[0] == '\0')
		return;

	if (SAMPLE_COMM_VI_SetIniPath(path) != CVI_SUCCESS)
		ZLOGE("SAMPLE_COMM_VI_SetIniPath(%s) failed", path);
	else
		ZLOGI("sensor ini override: %s", path);
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

static void fill_chn_attr(VPSS_CHN_ATTR_S *attr, const z_channel_profile_t *ch,
			  CVI_S32 fps, CVI_BOOL mirror, CVI_BOOL flip,
			  CVI_U32 depth, CVI_BOOL use_pre_rot_size,
			  CVI_U32 sensor_w, CVI_U32 sensor_h)
{
	memset(attr, 0, sizeof(*attr));
	if (use_pre_rot_size && ch->rotation != Z_ROTATION_0) {
		CVI_U32 pre_w, pre_h;
		gdc_pre_rot_size(sensor_w, sensor_h, &pre_w, &pre_h);
		attr->u32Width = pre_w;
		attr->u32Height = pre_h;
	} else {
		/* VPSS channel size = buffer size (hardware allocation). */
		attr->u32Width = ch->extent.buffer_width;
		attr->u32Height = ch->extent.buffer_height;
	}
	attr->enVideoFormat = VIDEO_FORMAT_LINEAR;
	attr->enPixelFormat = to_cvi_fmt(ch->pixel_format);
	attr->stFrameRate.s32SrcFrameRate = fps;
	attr->stFrameRate.s32DstFrameRate = fps;
	attr->u32Depth = depth;
	attr->bMirror = mirror;
	attr->bFlip = flip;
	if (ch->pixel_format == Z_PIXEL_RGB888 &&
	    ch->group_id == 2 && ch->channel_id == 0 &&
	    ch->extent.buffer_width != ch->extent.logical_width) {
		/* Display: fit into 192x320 buffer with black pad columns. */
		attr->stAspectRatio.enMode = ASPECT_RATIO_AUTO;
		attr->stAspectRatio.bEnableBgColor = CVI_TRUE;
		attr->stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
	} else if (ch->pixel_format == Z_PIXEL_YUV_NV21 && ch->rotation != Z_ROTATION_0) {
		attr->stAspectRatio.enMode = ASPECT_RATIO_AUTO;
		attr->stAspectRatio.bEnableBgColor = CVI_TRUE;
		attr->stAspectRatio.u32BgColor = COLOR_RGB_BLACK;
	} else {
		attr->stAspectRatio.enMode = ASPECT_RATIO_NONE;
		attr->stAspectRatio.bEnableBgColor = CVI_FALSE;
	}
	attr->stNormalize.bEnable = CVI_FALSE;
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
	/* Rotated input may need max of pre/post. */
	if (grp->input_type == Z_VPSS_INPUT_VPSS) {
		if (sensor_w > stVpssGrpAttr.u32MaxW)
			stVpssGrpAttr.u32MaxW = sensor_w;
		if (sensor_h > stVpssGrpAttr.u32MaxH)
			stVpssGrpAttr.u32MaxH = sensor_h;
		if (grp->input_extent.buffer_width > stVpssGrpAttr.u32MaxW)
			stVpssGrpAttr.u32MaxW = grp->input_extent.buffer_width;
		if (grp->input_extent.buffer_height > stVpssGrpAttr.u32MaxH)
			stVpssGrpAttr.u32MaxH = grp->input_extent.buffer_height;
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
		CVI_BOOL use_pre = (grp->group_id == 0 && ch->rotation != Z_ROTATION_0);
		CVI_BOOL should_enable;

		if (grp->group_id == 0)
			depth = 0;
		if (grp->group_id == 2 && ch->channel_id == 2)
			depth = 0;

		fill_chn_attr(&attr, ch, fps, mirror, flip, depth, use_pre, sensor_w, sensor_h);
		s32Ret = CVI_VPSS_SetChnAttr(gid, (VPSS_CHN)ch->channel_id, &attr);
		if (s32Ret != CVI_SUCCESS) {
			ZLOGE("%s SetChnAttr ch%u failed: 0x%x",
			      grp->node_name, ch->channel_id, s32Ret);
			return s32Ret;
		}

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
	stVbConf.astCommPool[4].u32BlkSize = half_blk;
	stVbConf.astCommPool[4].u32BlkCnt = 3;
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
	p->cam_w = 1080;
	p->cam_h = 1920;
	p->cam_fps = 30;
	p->mirror = CVI_FALSE;
	p->flip = CVI_FALSE;
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
		local.cam_w = 1080;
	if (local.cam_h == 0)
		local.cam_h = 1920;
	if (local.cam_fps == 0)
		local.cam_fps = 30;

	memset(&g_rt, 0, sizeof(g_rt));
	g_rt.mirror = local.mirror;
	g_rt.flip = local.flip;

	if (local.sensor_ini && local.sensor_ini[0])
		zonhor_graph_set_sensor_ini_path(local.sensor_ini);

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

	s32Ret = SAMPLE_COMM_VPSS_Bind_VPSS(2, 2, 3);
	if (s32Ret != CVI_SUCCESS) {
		ZLOGE("G2-ChC bind G3 failed: 0x%x", s32Ret);
		goto fail;
	}
	g_rt.g2_g3_bound = CVI_TRUE;
	g_rt.group_state[3] = Z_NODE_STATE_BOUND;

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

	if (!g_open || w == 0 || h == 0)
		return CVI_FAILURE;
	if (fps == 0)
		fps = g_rt.profile.sensor.fps;

	z_frame_layout_calc(w, h, &main_ext);

	CVI_VPSS_DisableChn(2, 1);
	memset(&attr, 0, sizeof(attr));
	attr.u32Width = main_ext.buffer_width;
	attr.u32Height = main_ext.buffer_height;
	attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
	attr.enPixelFormat = PIXEL_FORMAT_RGB_888;
	attr.stFrameRate.s32SrcFrameRate = fps;
	attr.stFrameRate.s32DstFrameRate = fps;
	attr.u32Depth = 1;
	attr.stAspectRatio.enMode = ASPECT_RATIO_NONE;
	ret = CVI_VPSS_SetChnAttr(2, 1, &attr);
	if (ret != CVI_SUCCESS) {
		CVI_VPSS_EnableChn(2, 1);
		return ret;
	}
	ret = CVI_VPSS_EnableChn(2, 1);
	if (ret == CVI_SUCCESS) {
		g_rt.profile.sensor.user_w = w;
		g_rt.profile.sensor.user_h = h;
		g_rt.profile.sensor.fps = fps;
		g_rt.profile.group2.channels[1].extent = main_ext;
		g_rt.chn_enabled[2][1] = CVI_TRUE;
	}
	return ret;
}

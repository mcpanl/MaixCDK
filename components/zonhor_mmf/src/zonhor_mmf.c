/**
 * Zonhor MMF facade — delegates to zonhor_graph_runtime.
 */

#include "zonhor_mmf.h"

#include <stdio.h>
#include <string.h>

void ZONHOR_MMF_DefaultConfig(ZONHOR_MMF_CFG_S *cfg)
{
	zonhor_graph_open_params_t p;

	if (!cfg)
		return;
	zonhor_graph_default_params(&p);
	memset(cfg, 0, sizeof(*cfg));
	cfg->cam_w = p.cam_w;
	cfg->cam_h = p.cam_h;
	cfg->cam_fps = p.cam_fps;
	cfg->mirror = p.mirror;
	cfg->flip = p.flip;
	cfg->sensor_ini = NULL;
}

void ZONHOR_MMF_SetSensorIniPath(const char *ini_path)
{
	zonhor_graph_set_sensor_ini_path(ini_path);
}

const char *ZONHOR_MMF_GetSensorIniPath(void)
{
	return zonhor_graph_get_sensor_ini_path();
}

CVI_S32 ZONHOR_MMF_Init(const ZONHOR_MMF_CFG_S *cfg)
{
	zonhor_graph_open_params_t p;

	zonhor_graph_default_params(&p);
	if (cfg) {
		p.cam_w = cfg->cam_w;
		p.cam_h = cfg->cam_h;
		p.cam_fps = cfg->cam_fps;
		p.mirror = cfg->mirror;
		p.flip = cfg->flip;
		p.sensor_ini = cfg->sensor_ini;
	}
	return zonhor_graph_open(&p);
}

void ZONHOR_MMF_Deinit(void)
{
	zonhor_graph_close();
}

CVI_BOOL ZONHOR_MMF_IsInited(void)
{
	return zonhor_graph_is_open();
}

void ZONHOR_MMF_GetSensorSize(CVI_U32 *w, CVI_U32 *h)
{
	zonhor_graph_get_sensor_size(w, h);
}

void ZONHOR_MMF_GetCamSize(CVI_U32 *w, CVI_U32 *h)
{
	zonhor_graph_get_user_size(w, h);
}

CVI_S32 ZONHOR_MMF_SetCamSize(CVI_U32 w, CVI_U32 h, CVI_S32 fps)
{
	return zonhor_graph_set_user_size(w, h, fps);
}

CVI_S32 ZONHOR_MMF_SetMirrorFlip(CVI_BOOL mirror, CVI_BOOL flip)
{
	return zonhor_graph_set_mirror_flip(mirror, flip);
}

CVI_S32 ZONHOR_MMF_CamGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
	return zonhor_graph_get_frame(Z_CAMERA_OUTPUT_MAIN_RGB, frame, timeout_ms);
}

CVI_S32 ZONHOR_MMF_CamReleaseFrame(VIDEO_FRAME_INFO_S *frame)
{
	return zonhor_graph_release_frame(Z_CAMERA_OUTPUT_MAIN_RGB, frame);
}

CVI_S32 ZONHOR_MMF_PreviewGetFrame(VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms)
{
	return zonhor_graph_get_frame(Z_CAMERA_OUTPUT_DISPLAY, frame, timeout_ms);
}

CVI_S32 ZONHOR_MMF_PreviewReleaseFrame(VIDEO_FRAME_INFO_S *frame)
{
	return zonhor_graph_release_frame(Z_CAMERA_OUTPUT_DISPLAY, frame);
}

CVI_S32 ZONHOR_MMF_GetOutputDesc(z_camera_output_id_t id, z_camera_output_desc_t *desc)
{
	return zonhor_graph_get_output_desc(id, desc);
}

CVI_S32 ZONHOR_MMF_EnableOutput(z_camera_output_id_t id)
{
	return zonhor_graph_enable_output(id);
}

CVI_S32 ZONHOR_MMF_DisableOutput(z_camera_output_id_t id)
{
	return zonhor_graph_disable_output(id);
}

CVI_S32 ZONHOR_MMF_GetVencBindInfo(z_camera_output_id_t id, VPSS_GRP *grp,
				   VPSS_CHN *chn, z_camera_output_desc_t *desc)
{
	z_camera_output_desc_t local;
	z_camera_output_desc_t *out = desc ? desc : &local;
	CVI_S32 ret;

	if (!grp || !chn)
		return CVI_FAILURE;

	ret = zonhor_graph_get_output_desc(id, out);
	if (ret != CVI_SUCCESS)
		return ret;

	*grp = (VPSS_GRP)out->group_id;
	*chn = (VPSS_CHN)out->channel_id;
	return CVI_SUCCESS;
}

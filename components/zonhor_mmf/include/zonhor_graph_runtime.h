/**
 * Zonhor media graph runtime — create / bind / start / stop from profile.
 */
#ifndef __ZONHOR_GRAPH_RUNTIME_H__
#define __ZONHOR_GRAPH_RUNTIME_H__

#include "zonhor_graph_profile.h"
#include "sample_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	Z_NODE_STATE_NONE = 0,
	Z_NODE_STATE_CREATED,
	Z_NODE_STATE_BOUND,
	Z_NODE_STATE_ENABLED,
	Z_NODE_STATE_RESERVED, /* created, intentionally not enabled */
} z_node_state_e;

typedef struct {
	z_node_state_e group_state[4];
	z_node_state_e channel_state[4][Z_VPSS_GRP_MAX_CHN];
	CVI_BOOL chn_enabled[4][VPSS_MAX_PHY_CHN_NUM];
	CVI_BOOL vi_inited;
	CVI_BOOL sys_inited;
	CVI_BOOL vi_vpss_bound;
	CVI_BOOL g0_g2_bound;
	CVI_BOOL g2_g3_bound;
	CVI_BOOL mirror;
	CVI_BOOL flip;
	zonhor_camera_graph_profile_t profile;
	SAMPLE_VI_CONFIG_S vi_config;
	SIZE_S sensor_size;
} zonhor_graph_runtime_t;

typedef struct {
	CVI_U32 cam_w;
	CVI_U32 cam_h;
	CVI_S32 cam_fps;
	CVI_BOOL mirror;
	CVI_BOOL flip;
	const char *sensor_ini;
} zonhor_graph_open_params_t;

void zonhor_graph_default_params(zonhor_graph_open_params_t *p);

zonhor_graph_runtime_t *zonhor_graph_runtime_get(void);

CVI_S32 zonhor_graph_open(const zonhor_graph_open_params_t *params);
void zonhor_graph_close(void);
CVI_BOOL zonhor_graph_is_open(void);

const zonhor_camera_graph_profile_t *zonhor_graph_profile(void);

CVI_S32 zonhor_graph_get_output_desc(z_camera_output_id_t id, z_camera_output_desc_t *desc);
CVI_S32 zonhor_graph_enable_output(z_camera_output_id_t id);
CVI_S32 zonhor_graph_disable_output(z_camera_output_id_t id);

CVI_S32 zonhor_graph_get_frame(z_camera_output_id_t id, VIDEO_FRAME_INFO_S *frame, CVI_S32 timeout_ms);
CVI_S32 zonhor_graph_release_frame(z_camera_output_id_t id, VIDEO_FRAME_INFO_S *frame);

CVI_S32 zonhor_graph_set_mirror_flip(CVI_BOOL mirror, CVI_BOOL flip);
CVI_S32 zonhor_graph_set_user_size(CVI_U32 w, CVI_U32 h, CVI_S32 fps);

void zonhor_graph_get_sensor_size(CVI_U32 *w, CVI_U32 *h);
void zonhor_graph_get_user_size(CVI_U32 *w, CVI_U32 *h);

void zonhor_graph_set_sensor_ini_path(const char *ini_path);
const char *zonhor_graph_get_sensor_ini_path(void);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_GRAPH_RUNTIME_H__ */

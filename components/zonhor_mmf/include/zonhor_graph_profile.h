/**
 * Zonhor camera graph profile — Group0~Group3 static topology definition.
 */
#ifndef __ZONHOR_GRAPH_PROFILE_H__
#define __ZONHOR_GRAPH_PROFILE_H__

#include "zonhor_frame_layout.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Z_VPSS_GRP_MAX_CHN 3

typedef enum {
	Z_VPSS_INPUT_VI = 0,
	Z_VPSS_INPUT_MEM,
	Z_VPSS_INPUT_VPSS,
} z_vpss_input_type_e;

typedef enum {
	Z_CAMERA_OUTPUT_DISPLAY = 0,
	Z_CAMERA_OUTPUT_MAIN_VENC,
	Z_CAMERA_OUTPUT_SUB_PIPELINE,
	Z_CAMERA_OUTPUT_SUB_VENC,
	Z_CAMERA_OUTPUT_SUB_NPU,
	Z_CAMERA_OUTPUT_USER_MEM,
	Z_CAMERA_OUTPUT_MAIN_RGB, /* Camera::read full-size RGB (Group2-ChB as RGB grab) */
	Z_CAMERA_OUTPUT_COUNT
} z_camera_output_id_t;

typedef enum {
	Z_PIXEL_YUV_NV21 = 0,
	Z_PIXEL_RGB888,
} z_pixel_format_e;

typedef struct {
	const char *name;
	uint32_t group_id;
	uint32_t channel_id;
	z_pixel_format_e pixel_format;
	z_frame_extent_t extent;
	bool create_only;
	bool enabled_by_default;
	z_rotation_e rotation; /* applied on this channel (GDC), usually 0 except Group0 */
} z_channel_profile_t;

typedef struct {
	const char *node_name;
	uint32_t device_id;
	uint32_t group_id;
	z_vpss_input_type_e input_type;
	uint32_t input_from_group;   /* when input is VPSS */
	uint32_t input_from_channel;
	z_frame_extent_t input_extent; /* max / expected input (sensor or upstream buffer) */
	uint32_t channel_count;
	z_channel_profile_t channels[Z_VPSS_GRP_MAX_CHN];
	bool create_only; /* whole group create-only (Group1) */
} z_vpss_group_profile_t;

typedef struct {
	uint32_t sensor_w; /* ISP / VI landscape width */
	uint32_t sensor_h;
	uint32_t user_w;   /* requested Camera user coords (often portrait) */
	uint32_t user_h;
	int32_t fps;
} z_sensor_caps_t;

typedef struct {
	uint32_t vi_mode;   /* informational: VI_ONLINE_VPSS_OFFLINE */
	uint32_t vpss_mode; /* informational: VPSS_MODE_DUAL */
	z_sensor_caps_t sensor;
	z_frame_extent_t rotated_main; /* Group0-Ch0 output extent */
	z_vpss_group_profile_t group0;
	z_vpss_group_profile_t group1;
	z_vpss_group_profile_t group2;
	z_vpss_group_profile_t group3;
} zonhor_camera_graph_profile_t;

typedef struct {
	z_camera_output_id_t id;
	const char *name;
	uint32_t group_id;
	uint32_t channel_id;
	z_frame_extent_t extent;
	z_pixel_format_e pixel_format;
	bool create_only;
	bool enabled_by_default;
} z_camera_output_desc_t;

/** Display panel logical size (JD9853). */
#define ZONHOR_DISP_LOGICAL_W 172u
#define ZONHOR_DISP_LOGICAL_H 320u

/**
 * Build full Group0~Group3 profile from sensor caps.
 * sensor_w/h are ISP landscape sizes; rotation is always 90° on Group0-Ch0
 * for the Zonhor portrait product orientation.
 */
int zonhor_build_camera_graph_profile(const z_sensor_caps_t *sensor_caps,
				      zonhor_camera_graph_profile_t *profile);

void zonhor_dump_camera_graph_profile(const zonhor_camera_graph_profile_t *profile);

int zonhor_profile_get_output(const zonhor_camera_graph_profile_t *profile,
			      z_camera_output_id_t id,
			      z_camera_output_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_GRAPH_PROFILE_H__ */

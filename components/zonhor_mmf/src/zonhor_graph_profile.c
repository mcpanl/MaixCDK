#include "zonhor_graph_profile.h"

#include <stdio.h>
#include <string.h>

static void fill_chn(z_channel_profile_t *ch, const char *name,
		     uint32_t gid, uint32_t cid,
		     z_pixel_format_e fmt, const z_frame_extent_t *ext,
		     bool create_only, bool enabled, z_rotation_e rot,
		     bool use_pre_rotation_attr_size)
{
	memset(ch, 0, sizeof(*ch));
	ch->name = name;
	ch->group_id = gid;
	ch->channel_id = cid;
	ch->pixel_format = fmt;
	ch->extent = *ext;
	ch->create_only = create_only;
	ch->enabled_by_default = enabled;
	ch->rotation = rot;
	ch->use_pre_rotation_attr_size = use_pre_rotation_attr_size;
}

int zonhor_build_camera_graph_profile(const z_sensor_caps_t *sensor_caps,
				      zonhor_camera_graph_profile_t *profile)
{
	z_frame_extent_t rot_ext;
	z_frame_extent_t disp_ext;
	z_frame_extent_t main_ext;
	z_frame_extent_t half_ext;
	z_frame_extent_t sensor_in;
	uint32_t sw, sh, uw, uh;

	if (!sensor_caps || !profile)
		return -1;

	memset(profile, 0, sizeof(*profile));
	profile->vi_mode = 1;   /* VI_ONLINE_VPSS_OFFLINE marker */
	profile->vpss_mode = 1; /* VPSS_MODE_DUAL marker */
	profile->sensor = *sensor_caps;

	sw = sensor_caps->sensor_w;
	sh = sensor_caps->sensor_h;
	uw = sensor_caps->user_w ? sensor_caps->user_w : sh; /* portrait default */
	uh = sensor_caps->user_h ? sensor_caps->user_h : sw;
	if (sw == 0 || sh == 0)
		return -1;

	/* Sensor VI input extent (landscape). */
	z_frame_layout_calc(sw, sh, &sensor_in);

	/*
	 * TEMP TEST (no-rotation CSC path):
	 * Group0-Ch0 outputs landscape NV21 1920x1080 WITHOUT GDC rotation.
	 * Group2-Ch0 scales that to RGB888 (no rotation).
	 * Revert this block after the experiment.
	 */
#if 0 /* TEMP_LANDSCAPE_CSC_TEST */
	{
		z_frame_extent_t rgb_ext;

		profile->rotated_main = sensor_in; /* name kept; content is non-rotated */

		/* Half-scale landscape RGB for a clear "scale-only" CSC check. */
		z_frame_layout_calc(sw / 2u, sh / 2u, &rgb_ext); /* 960x540 */
		disp_ext = rgb_ext;
		main_ext = rgb_ext;
		z_half_extent(&sensor_in, &half_ext);

		/* ---- Group0: Device0, VI input, NO rotation ---- */
		profile->group0.node_name = "Group0";
		profile->group0.device_id = 0;
		profile->group0.group_id = 0;
		profile->group0.input_type = Z_VPSS_INPUT_VI;
		profile->group0.input_extent = sensor_in;
		profile->group0.channel_count = 1;
		profile->group0.create_only = false;
		fill_chn(&profile->group0.channels[0], "landscape_main_yuv",
			 0, 0, Z_PIXEL_YUV_NV21, &sensor_in, false, true,
			 Z_ROTATION_0, false);

		/* ---- Group1: Device0, MEM, create only ---- */
		profile->group1.node_name = "Group1";
		profile->group1.device_id = 0;
		profile->group1.group_id = 1;
		profile->group1.input_type = Z_VPSS_INPUT_MEM;
		profile->group1.input_extent = sensor_in;
		profile->group1.channel_count = 1;
		profile->group1.create_only = true;
		fill_chn(&profile->group1.channels[0], "user_mem_pipeline",
			 1, 0, Z_PIXEL_YUV_NV21, &sensor_in, true, false,
			 Z_ROTATION_0, false);

		/* ---- Group2: Device1, from G0-Ch0 ---- */
		profile->group2.node_name = "Group2";
		profile->group2.device_id = 1;
		profile->group2.group_id = 2;
		profile->group2.input_type = Z_VPSS_INPUT_VPSS;
		profile->group2.input_from_group = 0;
		profile->group2.input_from_channel = 0;
		profile->group2.input_extent = sensor_in;
		profile->group2.channel_count = 3;
		profile->group2.create_only = false;
		fill_chn(&profile->group2.channels[0], "display_preview",
			 2, 0, Z_PIXEL_RGB888, &rgb_ext, false, true,
			 Z_ROTATION_0, false);
		/* Keep ch1 configured but disabled by runtime for a cleaner test. */
		fill_chn(&profile->group2.channels[1], "main_venc_input",
			 2, 1, Z_PIXEL_RGB888, &rgb_ext, false, false,
			 Z_ROTATION_0, false);
		fill_chn(&profile->group2.channels[2], "sub_pipeline_input",
			 2, 2, Z_PIXEL_YUV_NV21, &half_ext, false, false,
			 Z_ROTATION_0, false);

		/* ---- Group3: reserved (create-only channels) ---- */
		profile->group3.node_name = "Group3";
		profile->group3.device_id = 1;
		profile->group3.group_id = 3;
		profile->group3.input_type = Z_VPSS_INPUT_VPSS;
		profile->group3.input_from_group = 2;
		profile->group3.input_from_channel = 2;
		profile->group3.input_extent = half_ext;
		profile->group3.channel_count = 3;
		profile->group3.create_only = false;
		fill_chn(&profile->group3.channels[0], "sub_venc_input",
			 3, 0, Z_PIXEL_YUV_NV21, &half_ext, true, false,
			 Z_ROTATION_0, false);
		fill_chn(&profile->group3.channels[1], "sub_npu_input",
			 3, 1, Z_PIXEL_RGB888, &half_ext, true, false,
			 Z_ROTATION_0, false);
		{
			z_frame_extent_t reserved;
			z_frame_layout_calc(half_ext.logical_width / 2u,
					    half_ext.logical_height / 2u, &reserved);
			fill_chn(&profile->group3.channels[2], "sub_reserved",
				 3, 2, Z_PIXEL_YUV_NV21, &reserved, true, false,
				 Z_ROTATION_0, false);
		}

		printf("[zonhor_profile] TEMP_LANDSCAPE_CSC_TEST: "
		       "G0-Ch0 NV21 %ux%u no-rot -> G2-Ch0 RGB %ux%u scale-only\n",
		       sensor_in.logical_width, sensor_in.logical_height,
		       rgb_ext.logical_width, rgb_ext.logical_height);
		return 0;
	}
#endif

	/*
	 * Group0-Ch0: user wants logical portrait (H,W) e.g. 1080x1920.
	 * GDC stores buffer (align64(H),W) e.g. 1088x1920; SetChnAttr uses
	 * pre-rotation (W, align64(H)) + ASPECT_RATIO_AUTO + ROT90.
	 * Downstream Group2 must:
	 *   MaxW/H = buffer (1088x1920), SetGrpCrop = valid (logical crop).
	 */
	z_gdc_rot90_extent(sw, sh, Z_PAD_CENTER, &rot_ext);
	profile->rotated_main = rot_ext;

	/*
	 * Display ordinary VPSS channel: SetChnAttr uses logical 172x320.
	 * buffer 192 is storage/stride only; content is left-aligned at (0,0),
	 * NOT centered (the old valid_x=10 assumed a wrong 192-wide attr).
	 */
	z_frame_layout_calc(ZONHOR_DISP_LOGICAL_W, ZONHOR_DISP_LOGICAL_H, &disp_ext);

	/* Main full-size RGB / VENC path: user request size, 64-aligned buffer. */
	z_frame_layout_calc(uw, uh, &main_ext);

	/* Half-size for Group3 feed. */
	z_half_extent(&rot_ext, &half_ext);

	/* ---- Group0: Device0, VI input, single rotated YUV ---- */
	profile->group0.node_name = "Group0";
	profile->group0.device_id = 0;
	profile->group0.group_id = 0;
	profile->group0.input_type = Z_VPSS_INPUT_VI;
	profile->group0.input_extent = sensor_in;
	profile->group0.channel_count = 1;
	profile->group0.create_only = false;
	fill_chn(&profile->group0.channels[0], "rotated_main_yuv",
		 0, 0, Z_PIXEL_YUV_NV21, &rot_ext, false, true, Z_ROTATION_90, true);

	/* ---- Group1: Device0, MEM, create only ---- */
	profile->group1.node_name = "Group1";
	profile->group1.device_id = 0;
	profile->group1.group_id = 1;
	profile->group1.input_type = Z_VPSS_INPUT_MEM;
	profile->group1.input_extent = sensor_in;
	profile->group1.channel_count = 1;
	profile->group1.create_only = true;
	fill_chn(&profile->group1.channels[0], "user_mem_pipeline",
		 1, 0, Z_PIXEL_YUV_NV21, &sensor_in, true, false, Z_ROTATION_0, false);

	/* ---- Group2: Device1, from Group0-Ch0, distribute ---- */
	profile->group2.node_name = "Group2";
	profile->group2.device_id = 1;
	profile->group2.group_id = 2;
	profile->group2.input_type = Z_VPSS_INPUT_VPSS;
	profile->group2.input_from_group = 0;
	profile->group2.input_from_channel = 0;
	profile->group2.input_extent = rot_ext;
	profile->group2.channel_count = 3;
	profile->group2.create_only = false;
	fill_chn(&profile->group2.channels[0], "display_preview",
		 2, 0, Z_PIXEL_RGB888, &disp_ext, false, true, Z_ROTATION_0, false);
	fill_chn(&profile->group2.channels[1], "main_venc_input",
		 2, 1, Z_PIXEL_RGB888, &main_ext, false, true, Z_ROTATION_0, false);
	fill_chn(&profile->group2.channels[2], "sub_pipeline_input",
		 2, 2, Z_PIXEL_YUV_NV21, &half_ext, false, true, Z_ROTATION_0, false);

	/* ---- Group3: Device1, from Group2-ChC, reserved ---- */
	profile->group3.node_name = "Group3";
	profile->group3.device_id = 1;
	profile->group3.group_id = 3;
	profile->group3.input_type = Z_VPSS_INPUT_VPSS;
	profile->group3.input_from_group = 2;
	profile->group3.input_from_channel = 2;
	profile->group3.input_extent = half_ext;
	profile->group3.channel_count = 3;
	profile->group3.create_only = false;
	fill_chn(&profile->group3.channels[0], "sub_venc_input",
		 3, 0, Z_PIXEL_YUV_NV21, &half_ext, true, false, Z_ROTATION_0, false);
	fill_chn(&profile->group3.channels[1], "sub_npu_input",
		 3, 1, Z_PIXEL_RGB888, &half_ext, true, false, Z_ROTATION_0, false);
	{
		z_frame_extent_t reserved;
		z_frame_layout_calc(half_ext.logical_width / 2u,
				    half_ext.logical_height / 2u, &reserved);
		fill_chn(&profile->group3.channels[2], "sub_reserved",
			 3, 2, Z_PIXEL_YUV_NV21, &reserved, true, false, Z_ROTATION_0, false);
	}

	return 0;
}

void zonhor_dump_camera_graph_profile(const zonhor_camera_graph_profile_t *profile)
{
	const z_vpss_group_profile_t *groups[4];
	unsigned g, c;

	if (!profile)
		return;

	printf("[zonhor_profile] vi_mode=%u vpss_mode=%u sensor=%ux%u user=%ux%u fps=%d\n",
	       profile->vi_mode, profile->vpss_mode,
	       profile->sensor.sensor_w, profile->sensor.sensor_h,
	       profile->sensor.user_w, profile->sensor.user_h,
	       (int)profile->sensor.fps);
	z_frame_extent_print("rotated_main", &profile->rotated_main);

	groups[0] = &profile->group0;
	groups[1] = &profile->group1;
	groups[2] = &profile->group2;
	groups[3] = &profile->group3;

	for (g = 0; g < 4; ++g) {
		const z_vpss_group_profile_t *grp = groups[g];
		printf("[zonhor_profile] %s grp=%u dev=%u input=%u create_only=%d chn=%u\n",
		       grp->node_name, grp->group_id, grp->device_id,
		       (unsigned)grp->input_type, (int)grp->create_only,
		       grp->channel_count);
		z_frame_extent_print("  input", &grp->input_extent);
		for (c = 0; c < grp->channel_count; ++c) {
			const z_channel_profile_t *ch = &grp->channels[c];
			printf("  ch%u %s fmt=%u rot=%d create_only=%d enabled=%d pre_rot_attr=%d\n",
			       ch->channel_id, ch->name,
			       (unsigned)ch->pixel_format, (int)ch->rotation,
			       (int)ch->create_only, (int)ch->enabled_by_default,
			       (int)ch->use_pre_rotation_attr_size);
			z_frame_extent_print("    extent", &ch->extent);
		}
	}
}

int zonhor_profile_get_output(const zonhor_camera_graph_profile_t *profile,
			      z_camera_output_id_t id,
			      z_camera_output_desc_t *desc)
{
	const z_channel_profile_t *ch = NULL;

	if (!profile || !desc)
		return -1;
	memset(desc, 0, sizeof(*desc));
	desc->id = id;

	switch (id) {
	case Z_CAMERA_OUTPUT_DISPLAY:
		ch = &profile->group2.channels[0];
		desc->name = "display_preview";
		break;
	case Z_CAMERA_OUTPUT_MAIN_VENC:
	case Z_CAMERA_OUTPUT_MAIN_RGB:
#if 0 /* TEMP_LANDSCAPE_CSC_TEST: read scale-only RGB from G2-Ch0 */
		ch = &profile->group2.channels[0];
		desc->name = (id == Z_CAMERA_OUTPUT_MAIN_RGB) ? "main_rgb" : "main_venc_input";
#else
		ch = &profile->group2.channels[1];
		desc->name = (id == Z_CAMERA_OUTPUT_MAIN_RGB) ? "main_rgb" : "main_venc_input";
#endif
		break;
	case Z_CAMERA_OUTPUT_SUB_PIPELINE:
		ch = &profile->group2.channels[2];
		desc->name = "sub_pipeline_input";
		break;
	case Z_CAMERA_OUTPUT_SUB_VENC:
		ch = &profile->group3.channels[0];
		desc->name = "sub_venc_input";
		break;
	case Z_CAMERA_OUTPUT_SUB_NPU:
		ch = &profile->group3.channels[1];
		desc->name = "sub_npu_input";
		break;
	case Z_CAMERA_OUTPUT_USER_MEM:
		ch = &profile->group1.channels[0];
		desc->name = "user_mem_pipeline";
		break;
	default:
		return -1;
	}

	desc->group_id = ch->group_id;
	desc->channel_id = ch->channel_id;
	desc->extent = ch->extent;
	desc->pixel_format = ch->pixel_format;
	desc->create_only = ch->create_only;
	desc->enabled_by_default = ch->enabled_by_default;
	return 0;
}

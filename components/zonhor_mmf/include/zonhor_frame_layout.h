/**
 * Unified frame extent / alignment helpers for Zonhor media graph.
 *
 * Contract (must not mix layers):
 *   logical_*  = user / product picture size (e.g. 1080x1920)
 *   buffer_*   = VPSS/VB storage size; width is 64-aligned (e.g. 1088x1920)
 *   valid_*    = crop rect of logical content inside the buffer
 *
 * Downstream VPSS←VPSS groups:
 *   GrpAttr.u32MaxW/H  = buffer_* of the bound input (never lift to sensor_w)
 *   SetGrpCrop          = valid_* when buffer has padding
 *   SetChnAttr (normal) = logical_* (never write buffer_* as output size)
 */
#ifndef __ZONHOR_FRAME_LAYOUT_H__
#define __ZONHOR_FRAME_LAYOUT_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define Z_ALIGN_64 64u

typedef struct {
	uint32_t logical_width;
	uint32_t logical_height;
	uint32_t buffer_width;
	uint32_t buffer_height;
	uint32_t valid_x;
	uint32_t valid_y;
	uint32_t valid_width;
	uint32_t valid_height;
} z_frame_extent_t;

typedef enum {
	Z_ROTATION_0 = 0,
	Z_ROTATION_90 = 90,
	Z_ROTATION_180 = 180,
	Z_ROTATION_270 = 270,
} z_rotation_e;

/** How letterbox padding is placed inside buffer when buffer_w > logical_w. */
typedef enum {
	Z_PAD_CENTER = 0,   /* valid_x = (buffer-logical)/2  — matches ASPECT_RATIO_AUTO */
	Z_PAD_LEFT_TOP = 1, /* valid at (0,0); pad on right/bottom */
} z_pad_mode_e;

uint32_t z_align_up(uint32_t value, uint32_t align);
uint32_t z_align_up_64(uint32_t value);

/** Fill buffer_* from logical_* (width 64-aligned); valid rect = full logical at (0,0). */
void z_frame_layout_calc(uint32_t logical_w, uint32_t logical_h, z_frame_extent_t *out);

/** Apply rotation to logical size, then re-run width-64 buffer layout. */
void z_rotate_extent(uint32_t in_w, uint32_t in_h, z_rotation_e rot, z_frame_extent_t *out);

/**
 * Sensor landscape (W,H) → GDC ROT90 portrait extent.
 * User sees logical (H,W); VPSS stores buffer (align64(H), W); valid_* crops
 * the letterboxed logical picture out of that buffer for SetGrpCrop.
 *
 * Example 1920x1080 → logical 1080x1920, buffer 1088x1920,
 * valid=(4,0,1080,1920) when pad=Z_PAD_CENTER.
 */
void z_gdc_rot90_extent(uint32_t sensor_w, uint32_t sensor_h,
			z_pad_mode_e pad, z_frame_extent_t *out);

/**
 * Scale into a target logical viewport; buffer width 64-aligned.
 * valid_* describes the letterboxed content rect inside the buffer when
 * letterbox is used; for FIT_FILL, valid covers the full logical area.
 */
void z_scale_extent(uint32_t src_w, uint32_t src_h,
		    uint32_t dst_logical_w, uint32_t dst_logical_h,
		    bool letterbox, z_frame_extent_t *out);

/** Mark padding: valid rect inside buffer (typically left-aligned content). */
void z_apply_padding(z_frame_extent_t *extent, uint32_t valid_x, uint32_t valid_y,
		     uint32_t valid_w, uint32_t valid_h);

/** True when buffer is larger than the valid crop (GrpCrop needed). */
bool z_extent_needs_crop(const z_frame_extent_t *extent);

/** Half-size of src for SUB_VENC / G3 feed.
 * Width is rounded UP to 64 and height is adjusted to keep aspect ratio so
 * logical_* == buffer_* (no right-edge stride padding). Board-proven: a
 * 540-wide NV21 channel with buffer/stride 576 encodes a green/junk strip
 * inside the right ~36 px that VENC crop does not remove on the bind path.
 * Example: 1080x1920 → 576x1024 (not 540x960 / buffer 576x960).
 */
void z_half_extent(const z_frame_extent_t *src, z_frame_extent_t *out);

void z_frame_extent_print(const char *tag, const z_frame_extent_t *e);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_FRAME_LAYOUT_H__ */

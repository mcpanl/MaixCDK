/**
 * Unified frame extent / alignment helpers for Zonhor media graph.
 * logical_* = effective picture; buffer_* = allocated size (width 64-aligned).
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

uint32_t z_align_up(uint32_t value, uint32_t align);
uint32_t z_align_up_64(uint32_t value);

/** Fill buffer_* from logical_* (width 64-aligned); valid rect = full logical at (0,0). */
void z_frame_layout_calc(uint32_t logical_w, uint32_t logical_h, z_frame_extent_t *out);

/** Apply rotation to logical size, then re-run width-64 buffer layout. */
void z_rotate_extent(uint32_t in_w, uint32_t in_h, z_rotation_e rot, z_frame_extent_t *out);

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

/** Half-size of src (integer divide), then 64-align buffer width. */
void z_half_extent(const z_frame_extent_t *src, z_frame_extent_t *out);

void z_frame_extent_print(const char *tag, const z_frame_extent_t *e);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_FRAME_LAYOUT_H__ */

#include "zonhor_frame_layout.h"

#include <stdio.h>
#include <string.h>

uint32_t z_align_up(uint32_t value, uint32_t align)
{
	if (align == 0)
		return value;
	return (value + align - 1u) / align * align;
}

uint32_t z_align_up_64(uint32_t value)
{
	return z_align_up(value, Z_ALIGN_64);
}

void z_frame_layout_calc(uint32_t logical_w, uint32_t logical_h, z_frame_extent_t *out)
{
	if (!out)
		return;
	memset(out, 0, sizeof(*out));
	out->logical_width = logical_w;
	out->logical_height = logical_h;
	out->buffer_width = z_align_up_64(logical_w);
	out->buffer_height = logical_h;
	out->valid_x = 0;
	out->valid_y = 0;
	out->valid_width = logical_w;
	out->valid_height = logical_h;
}

void z_rotate_extent(uint32_t in_w, uint32_t in_h, z_rotation_e rot, z_frame_extent_t *out)
{
	uint32_t lw, lh;

	if (!out)
		return;

	switch (rot) {
	case Z_ROTATION_90:
	case Z_ROTATION_270:
		lw = in_h;
		lh = in_w;
		break;
	case Z_ROTATION_180:
	case Z_ROTATION_0:
	default:
		lw = in_w;
		lh = in_h;
		break;
	}
	/* Critical: re-check width alignment after rotation swaps axes. */
	z_frame_layout_calc(lw, lh, out);
}

void z_gdc_rot90_extent(uint32_t sensor_w, uint32_t sensor_h,
			z_pad_mode_e pad, z_frame_extent_t *out)
{
	uint32_t pad_x = 0;

	if (!out)
		return;

	/*
	 * User wants portrait (sensor_h x sensor_w), e.g. 1080x1920.
	 * GDC stores align64(sensor_h) x sensor_w, e.g. 1088x1920.
	 * Pre-rot SetChnAttr is (sensor_w, align64(sensor_h)) + ASPECT_RATIO_AUTO
	 * so content letterboxes into that canvas; after ROT90 the pad is on X.
	 */
	z_rotate_extent(sensor_w, sensor_h, Z_ROTATION_90, out);

	if (out->buffer_width <= out->logical_width)
		return;

	if (pad == Z_PAD_LEFT_TOP)
		pad_x = 0;
	else
		pad_x = (out->buffer_width - out->logical_width) / 2u;

	z_apply_padding(out, pad_x, 0, out->logical_width, out->logical_height);
}

void z_scale_extent(uint32_t src_w, uint32_t src_h,
		    uint32_t dst_logical_w, uint32_t dst_logical_h,
		    bool letterbox, z_frame_extent_t *out)
{
	if (!out)
		return;

	z_frame_layout_calc(dst_logical_w, dst_logical_h, out);

	if (!letterbox || src_w == 0 || src_h == 0) {
		out->valid_x = 0;
		out->valid_y = 0;
		out->valid_width = dst_logical_w;
		out->valid_height = dst_logical_h;
		return;
	}

	/* Content fitted inside logical viewport; padding is outside valid_*. */
	{
		uint64_t dst_a = (uint64_t)dst_logical_w * src_h;
		uint64_t src_a = (uint64_t)src_w * dst_logical_h;
		uint32_t fitted_w, fitted_h, ox, oy;

		if (dst_a >= src_a) {
			/* letterbox top/bottom or exact */
			fitted_h = dst_logical_h;
			fitted_w = (uint32_t)(((uint64_t)src_w * dst_logical_h) / src_h);
			if (fitted_w > dst_logical_w)
				fitted_w = dst_logical_w;
			ox = (dst_logical_w - fitted_w) / 2u;
			oy = 0;
		} else {
			fitted_w = dst_logical_w;
			fitted_h = (uint32_t)(((uint64_t)src_h * dst_logical_w) / src_w);
			if (fitted_h > dst_logical_h)
				fitted_h = dst_logical_h;
			ox = 0;
			oy = (dst_logical_h - fitted_h) / 2u;
		}
		out->valid_x = ox;
		out->valid_y = oy;
		out->valid_width = fitted_w;
		out->valid_height = fitted_h;
	}
}

void z_apply_padding(z_frame_extent_t *extent, uint32_t valid_x, uint32_t valid_y,
		     uint32_t valid_w, uint32_t valid_h)
{
	if (!extent)
		return;
	extent->valid_x = valid_x;
	extent->valid_y = valid_y;
	extent->valid_width = valid_w;
	extent->valid_height = valid_h;
}

bool z_extent_needs_crop(const z_frame_extent_t *extent)
{
	if (!extent || extent->valid_width == 0 || extent->valid_height == 0)
		return false;
	if (extent->valid_width == extent->buffer_width &&
	    extent->valid_height == extent->buffer_height &&
	    extent->valid_x == 0 && extent->valid_y == 0)
		return false;
	return true;
}

void z_half_extent(const z_frame_extent_t *src, z_frame_extent_t *out)
{
	uint32_t lw, lh, aligned_w, aligned_h;

	if (!src || !out)
		return;
	lw = src->logical_width / 2u;
	lh = src->logical_height / 2u;
	if (lw < 2u)
		lw = 2u;
	if (lh < 2u)
		lh = 2u;
	/* Keep even dims for YUV. */
	lw &= ~1u;
	lh &= ~1u;

	/*
	 * VENC bind path encodes the full channel buffer. If logical_w is not
	 * 64-aligned, z_frame_layout_calc() makes buffer_w > logical_w and the
	 * right padding (often uninit / green) becomes a visible side strip —
	 * even when PicWidth/crop is set to logical_w.
	 *
	 * Round width UP to 64 and scale height to preserve aspect so
	 * logical == buffer and valid fills the frame.
	 */
	aligned_w = z_align_up_64(lw);
	if (aligned_w != lw && lw > 0) {
		aligned_h = (uint32_t)(((uint64_t)aligned_w * lh) / lw);
		aligned_h &= ~1u;
		if (aligned_h < 2u)
			aligned_h = 2u;
	} else {
		aligned_h = lh;
	}

	z_frame_layout_calc(aligned_w, aligned_h, out);
}

void z_frame_extent_print(const char *tag, const z_frame_extent_t *e)
{
	if (!e)
		return;
	printf("[z_extent] %s logical=%ux%u buffer=%ux%u valid=(%u,%u %ux%u)\n",
	       tag ? tag : "?",
	       e->logical_width, e->logical_height,
	       e->buffer_width, e->buffer_height,
	       e->valid_x, e->valid_y, e->valid_width, e->valid_height);
}

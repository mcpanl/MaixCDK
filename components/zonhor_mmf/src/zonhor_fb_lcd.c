/**
 * Framebuffer LCD helpers for Zonhor JD9853 (172x320 RGB565 /dev/fb0).
 * Ported from sample_sensor_lcd fb_lcd.c (perf/HUD stripped).
 */

#include "zonhor_mmf.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/fb.h>

int ZONHOR_FB_Open(ZONHOR_FB_LCD_S *fb, const char *dev)
{
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	const char *path = (dev && dev[0]) ? dev : ZONHOR_FB_LCD_DEV;

	if (!fb)
		return -1;
	memset(fb, 0, sizeof(*fb));
	fb->fd = -1;

	fb->fd = open(path, O_RDWR);
	if (fb->fd < 0) {
		perror("ZONHOR_FB_Open");
		return -1;
	}

	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
	    ioctl(fb->fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("ZONHOR_FB_Open ioctl");
		close(fb->fd);
		fb->fd = -1;
		return -1;
	}

	fb->width = (int)var.xres;
	fb->height = (int)var.yres;
	fb->line_length = (int)fix.line_length;
	if (fb->width <= 0)
		fb->width = ZONHOR_FB_LCD_WIDTH;
	if (fb->height <= 0)
		fb->height = ZONHOR_FB_LCD_HEIGHT;
	if (fb->line_length < fb->width * 2)
		fb->line_length = fb->width * 2;

	fb->size = fix.smem_len;
	if (fb->size < (size_t)fb->line_length * (size_t)fb->height)
		fb->size = (size_t)fb->line_length * (size_t)fb->height;

	fb->fb = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->fb == MAP_FAILED) {
		perror("ZONHOR_FB_Open mmap");
		close(fb->fd);
		fb->fd = -1;
		fb->fb = NULL;
		return -1;
	}
	return 0;
}

void ZONHOR_FB_Close(ZONHOR_FB_LCD_S *fb)
{
	if (!fb)
		return;
	if (fb->fb && fb->fb != MAP_FAILED) {
		munmap(fb->fb, fb->size);
		fb->fb = NULL;
	}
	if (fb->fd >= 0) {
		close(fb->fd);
		fb->fd = -1;
	}
}

void ZONHOR_FB_Clear(ZONHOR_FB_LCD_S *fb, uint16_t color)
{
	int y, x;

	if (!fb || !fb->fb)
		return;
	for (y = 0; y < fb->height; y++) {
		uint16_t *row = (uint16_t *)((uint8_t *)fb->fb + y * fb->line_length);
		for (x = 0; x < fb->width; x++)
			row[x] = color;
	}
}

void ZONHOR_FB_DrawRgb565(ZONHOR_FB_LCD_S *fb, const uint16_t *src, int src_w, int src_h)
{
	int x, y;
	int crop_x = 0, crop_y = 0;
	int draw_w, draw_h, off_x, off_y;

	if (!fb || !fb->fb || !src || src_w <= 0 || src_h <= 0)
		return;

	draw_w = src_w;
	draw_h = src_h;
	if (draw_w > fb->width) {
		crop_x = (draw_w - fb->width) / 2;
		draw_w = fb->width;
	}
	if (draw_h > fb->height) {
		crop_y = (draw_h - fb->height) / 2;
		draw_h = fb->height;
	}
	off_x = (fb->width - draw_w) / 2;
	off_y = (fb->height - draw_h) / 2;

	for (y = 0; y < draw_h; y++) {
		int src_y = crop_y + y;
		// int dst_y = fb->height - 1 - (off_y + y);
		int dst_y = off_y + y;
		const uint16_t *src_row = src + src_y * src_w + crop_x;
		uint16_t *dst_row = (uint16_t *)((uint8_t *)fb->fb + dst_y * fb->line_length);

		for (x = 0; x < draw_w; x++)
			dst_row[off_x + x] = src_row[x];
	}
}

/* 8x8 bitmap font (from sample_sensor_lcd / screen_demo), MSB=left. */
static const uint8_t g_font8[128][8] = {
	['0'] = { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 },
	['1'] = { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 },
	['2'] = { 0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00 },
	['3'] = { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 },
	['4'] = { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00 },
	['5'] = { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 },
	['6'] = { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 },
	['7'] = { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 },
	['8'] = { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 },
	['9'] = { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 },
	['%'] = { 0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00 },
	['C'] = { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 },
	['-'] = { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 },
};

#define HUD_INSET_X 10
#define HUD_INSET_Y 12
#define HUD_CHAR_W  8
#define HUD_CHAR_H  8

static uint16_t *fb_row_top(ZONHOR_FB_LCD_S *fb, int y_top)
{
	int py = fb->height - 1 - y_top;
	return (uint16_t *)((uint8_t *)fb->fb + py * fb->line_length);
}

static void fb_fill_rect(ZONHOR_FB_LCD_S *fb, int x, int y, int w, int h, uint16_t color)
{
	int yy, xx;
	for (yy = y; yy < y + h; yy++) {
		uint16_t *row = fb_row_top(fb, yy);
		for (xx = x; xx < x + w; xx++)
			row[xx] = color;
	}
}

static void fb_draw_char(ZONHOR_FB_LCD_S *fb, int x, int y, char ch, uint16_t fg, uint16_t bg)
{
	unsigned idx = (unsigned char)ch;
	const uint8_t *glyph;
	int row, col, any = 0;

	if (idx >= 128)
		return;
	glyph = g_font8[idx];
	for (row = 0; row < 8; row++)
		any |= glyph[row];
	if (!any)
		return;

	for (row = 0; row < 8; row++) {
		uint8_t bits = glyph[row];
		uint16_t *dst = fb_row_top(fb, y + row);
		for (col = 0; col < 8; col++)
			dst[x + col] = (bits & (1u << (7 - col))) ? fg : bg;
	}
}

static void fb_draw_text(ZONHOR_FB_LCD_S *fb, int x, int y, const char *text,
			 uint16_t fg, uint16_t bg)
{
	int cx = x;
	const char *p;
	if (!fb || !text)
		return;
	for (p = text; *p; p++) {
		if (*p == ' ') {
			cx += 8;
			continue;
		}
		fb_draw_char(fb, cx, y, *p, fg, bg);
		cx += 8;
	}
}

static uint16_t fb_battery_color(int pct)
{
	if (pct <= 15)
		return 0xF800; /* red */
	if (pct <= 30)
		return 0xFD20; /* orange */
	if (pct <= 60)
		return 0xFFE0; /* yellow */
	return 0x07E0;         /* green */
}

void ZONHOR_FB_DrawStatusHud(ZONHOR_FB_LCD_S *fb, int bat_valid, int bat_pct,
			     int temp_valid, int temp_c)
{
	char bat_text[8];
	char temp_text[8];
	int bat_w, temp_w, bat_x, temp_x;
	int bat_y = HUD_INSET_Y;
	int temp_y;

	if (!fb || !fb->fb)
		return;

	temp_y = fb->height - HUD_INSET_Y - HUD_CHAR_H;

	if (bat_valid)
		snprintf(bat_text, sizeof(bat_text), "%d%%", bat_pct);
	else
		snprintf(bat_text, sizeof(bat_text), "--");
	if (temp_valid)
		snprintf(temp_text, sizeof(temp_text), "%dC", temp_c);
	else
		snprintf(temp_text, sizeof(temp_text), "--C");

	bat_w = (int)strlen(bat_text) * HUD_CHAR_W;
	temp_w = (int)strlen(temp_text) * HUD_CHAR_W;
	bat_x = fb->width - HUD_INSET_X - bat_w;
	temp_x = HUD_INSET_X;

	fb_fill_rect(fb, bat_x - 1, bat_y - 1, bat_w + 2, HUD_CHAR_H + 2, 0x0000);
	fb_fill_rect(fb, temp_x - 1, temp_y - 1, temp_w + 2, HUD_CHAR_H + 2, 0x0000);

	fb_draw_text(fb, bat_x, bat_y, bat_text,
		     bat_valid ? fb_battery_color(bat_pct) : 0xFFFF, 0x0000);
	fb_draw_text(fb, temp_x, temp_y, temp_text, 0x07FF /* cyan */, 0x0000);
}

int ZONHOR_RGB888_ToRgb565(const VIDEO_FRAME_S *frame, uint16_t *dst)
{
	const uint8_t *src;
	CVI_U32 w, h, stride;
	int x, y;

	if (!frame || !dst)
		return -1;
	if (frame->enPixelFormat != PIXEL_FORMAT_RGB_888)
		return -1;
	if (!frame->pu8VirAddr[0])
		return -1;

	w = frame->u32Width;
	h = frame->u32Height;
	if (w == 0 || h == 0)
		return -1;

	stride = frame->u32Stride[0];
	src = frame->pu8VirAddr[0];

	for (y = 0; y < (int)h; y++) {
		const uint8_t *src_row = src + y * (int)stride;
		uint16_t *dst_row = dst + y * (int)w;

		for (x = 0; x < (int)w; x++) {
			uint8_t R = src_row[x * 3 + 0];
			uint8_t G = src_row[x * 3 + 1];
			uint8_t B = src_row[x * 3 + 2];

			dst_row[x] = (uint16_t)(((R & 0xF8) << 8) |
						 ((G & 0xFC) << 3) |
						 (B >> 3));
		}
	}
	return 0;
}

static CVI_S32 map_frame(VIDEO_FRAME_INFO_S *pstFrame, void **ppVir, size_t *pSize)
{
	size_t image_size;
	CVI_U32 plane_offset = 0;
	int i;

	image_size = pstFrame->stVFrame.u32Length[0] + pstFrame->stVFrame.u32Length[1]
		     + pstFrame->stVFrame.u32Length[2];
	if (image_size == 0)
		return CVI_FAILURE;

	*ppVir = CVI_SYS_Mmap(pstFrame->stVFrame.u64PhyAddr[0], image_size);
	if (*ppVir == NULL)
		return CVI_FAILURE;

	CVI_SYS_IonInvalidateCache(pstFrame->stVFrame.u64PhyAddr[0], *ppVir, image_size);

	for (i = 0; i < 3; i++) {
		if (pstFrame->stVFrame.u32Length[i] != 0) {
			pstFrame->stVFrame.pu8VirAddr[i] = (CVI_U8 *)*ppVir + plane_offset;
			plane_offset += pstFrame->stVFrame.u32Length[i];
		}
	}
	*pSize = image_size;
	return CVI_SUCCESS;
}

int ZONHOR_FB_BlitPreview(ZONHOR_FB_LCD_S *fb, uint16_t *rgb565_scratch,
			  size_t scratch_pixels, CVI_S32 timeout_ms)
{
	VIDEO_FRAME_INFO_S stFrame;
	void *vir = NULL;
	size_t map_size = 0;
	CVI_S32 ret;
	CVI_U32 fw, fh;

	if (!fb || !rgb565_scratch)
		return -1;

	memset(&stFrame, 0, sizeof(stFrame));
	ret = ZONHOR_MMF_PreviewGetFrame(&stFrame, timeout_ms);
	if (ret != CVI_SUCCESS)
		return -2;

	fw = stFrame.stVFrame.u32Width;
	fh = stFrame.stVFrame.u32Height;
	if (fw == 0 || fh == 0 || (size_t)fw * fh > scratch_pixels) {
		ZONHOR_MMF_PreviewReleaseFrame(&stFrame);
		return -3;
	}

	if (map_frame(&stFrame, &vir, &map_size) != CVI_SUCCESS) {
		ZONHOR_MMF_PreviewReleaseFrame(&stFrame);
		return -4;
	}

	if (ZONHOR_RGB888_ToRgb565(&stFrame.stVFrame, rgb565_scratch) == 0) {
		ZONHOR_FB_DrawRgb565(fb, rgb565_scratch, (int)fw, (int)fh);
		/* Optional HUD overlay is drawn by caller after BlitPreview. */
	}

	CVI_SYS_Munmap(vir, map_size);
	ZONHOR_MMF_PreviewReleaseFrame(&stFrame);
	return 0;
}

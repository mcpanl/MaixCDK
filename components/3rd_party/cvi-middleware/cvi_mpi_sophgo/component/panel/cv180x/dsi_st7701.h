#ifndef _MIPI_TX_PARAM_ST_7701_H_
#define _MIPI_TX_PARAM_ST_7701_H_

#include <linux/cvi_comm_mipi_tx.h>

#define PANEL_NAME "NETEASE-2"

#define ST7701_NETEASE_VACT		640
#define ST7701_NETEASE_VSA		10
#define ST7701_NETEASE_VBP		20
#define ST7701_NETEASE_VFP		20

#define ST7701_NETEASE_HACT		480
#define ST7701_NETEASE_HSA		10
#define ST7701_NETEASE_HBP		60
#define ST7701_NETEASE_HFP		60

#define PIXEL_CLK(x) ((x##_VACT + x##_VSA + x##_VBP + x##_VFP) \
	* (x##_HACT + x##_HSA + x##_HBP + x##_HFP) * 60 / 1000)

struct combo_dev_cfg_s dev_cfg_st7701_480x640 = {
	.devno = 0,
	.lane_id = {MIPI_TX_LANE_0, MIPI_TX_LANE_1, MIPI_TX_LANE_CLK, -1, -1},
	.lane_pn_swap = {true, true, true, true, true},
	.output_mode = OUTPUT_MODE_DSI_VIDEO,
	.video_mode = BURST_MODE,
	.output_format = OUT_FORMAT_RGB_24_BIT,
	.sync_info = {
		.vid_hsa_pixels = ST7701_NETEASE_HSA,
		.vid_hbp_pixels = ST7701_NETEASE_HBP,
		.vid_hfp_pixels = ST7701_NETEASE_HFP,
		.vid_hline_pixels = ST7701_NETEASE_HACT,
		.vid_vsa_lines = ST7701_NETEASE_VSA,
		.vid_vbp_lines = ST7701_NETEASE_VBP,
		.vid_vfp_lines = ST7701_NETEASE_VFP,
		.vid_active_lines = ST7701_NETEASE_VACT,
		.vid_vsa_pos_polarity = true,
		.vid_hsa_pos_polarity = false,
	},
	.pixel_clk = PIXEL_CLK(ST7701_NETEASE),
};

#if 1
const struct hs_settle_s hs_timing_cfg_st7701_480x640 = { .prepare = 6, .zero = 32, .trail = 1 };
//zhengsao
static CVI_U8 data_st7701_0[]  = { 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13 };
static CVI_U8 data_st7701_1[]  = { 0xEF, 0x08 };
static CVI_U8 data_st7701_2[]  = { 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10 };

static CVI_U8 data_st7701_3[]  = { 0xC0, 0x4F, 0x00 };
static CVI_U8 data_st7701_4[]  = { 0xC1, 0x15, 0x02 };
static CVI_U8 data_st7701_5[]  = { 0xC2, 0x07, 0x0F };
static CVI_U8 data_st7701_6[]  = { 0xCC, 0x10 };

static CVI_U8 data_st7701_7[]  = {
    0xB0,
    0xC0, 0x04, 0x4C, 0x09, 0x8D, 0x06, 0x02,
    0x06, 0x06, 0x1E, 0x06, 0x15, 0x11, 0x24,
    0xA7, 0x9F
};

static CVI_U8 data_st7701_8[]  = {
    0xB1,
    0xC0, 0x13, 0x59, 0x14, 0x16, 0x08, 0x05,
    0x09, 0x09, 0x1D, 0x05, 0x14, 0x14, 0xA1,
    0xA4, 0x4D
};

static CVI_U8 data_st7701_9[] = { 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11 };

static CVI_U8 data_st7701_10[] = { 0xB0, 0x57 };
static CVI_U8 data_st7701_11[] = { 0xB1, 0x53 };
static CVI_U8 data_st7701_12[] = { 0xB2, 0x85 };
static CVI_U8 data_st7701_13[] = { 0xB3, 0x80 };
static CVI_U8 data_st7701_14[] = { 0xB5, 0x4B };
static CVI_U8 data_st7701_15[] = { 0xB7, 0x85 };
static CVI_U8 data_st7701_16[] = { 0xB8, 0x21 };
static CVI_U8 data_st7701_17[] = { 0xB9, 0x10 };

static CVI_U8 data_st7701_18[] = { 0xC1, 0x78 };
static CVI_U8 data_st7701_19[] = { 0xC2, 0x78 };
static CVI_U8 data_st7701_20[] = { 0xD0, 0x88 };

static CVI_U8 data_st7701_21[] = { 0xE0, 0x00, 0x00, 0x02 };

static CVI_U8 data_st7701_22[] = {
    0xE1,
    0x01, 0xA0, 0x00, 0xA0, 0x01, 0xA0,
    0x00, 0xA0, 0x00, 0x84, 0x84
};

static CVI_U8 data_st7701_23[] = {
    0xE2,
    0x02, 0x02, 0x64, 0x64, 0x0E, 0xA0,
    0x93, 0xA0, 0x0F, 0xA0, 0x94, 0xA0,
    0x00
};

static CVI_U8 data_st7701_24[] = { 0xE3, 0x00, 0x00, 0x22, 0x22 };
static CVI_U8 data_st7701_25[] = { 0xE4, 0x44, 0x44 };

static CVI_U8 data_st7701_26[] = {
    0xE5,
    0x0F, 0x93, 0x30, 0xC0,
    0x11, 0x95, 0x30, 0xC0,
    0x13, 0x97, 0x30, 0xC0,
    0x15, 0x99, 0x30, 0xC0
};

static CVI_U8 data_st7701_27[] = { 0xE6, 0x00, 0x00, 0x22, 0x22 };
static CVI_U8 data_st7701_28[] = { 0xE7, 0x44, 0x44 };

static CVI_U8 data_st7701_29[] = {
    0xE8,
    0x10, 0x94, 0x30, 0xC0,
    0x12, 0x96, 0x30, 0xC0,
    0x14, 0x98, 0x30, 0xC0,
    0x16, 0x9A, 0x30, 0xC0
};

static CVI_U8 data_st7701_30[] = { 0xE9, 0x36, 0x00, 0x00 };

static CVI_U8 data_st7701_31[] = {
    0xEB,
    0x00, 0x01, 0x4E, 0x4E, 0x00, 0xEE, 0x40
};

static CVI_U8 data_st7701_32[] = { 0xEC, 0x78, 0x00, 0x00 };

static CVI_U8 data_st7701_33[] = {
    0xED,
    0xF0, 0xFF, 0xF3, 0x76, 0x54, 0xB2,
    0xFF, 0xFF, 0xFF, 0xFF,
    0x2B, 0x45, 0x67, 0x3F, 0xFF, 0x0F
};

static CVI_U8 data_st7701_34[] = {
    0xEF, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F
};

static CVI_U8 data_st7701_35[] = { 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00 };
static CVI_U8 data_st7701_36[] = { 0x11 };        /* Delay 120ms */
static CVI_U8 data_st7701_37[] = { 0x36, 0x00 };  /* Normal scan */
static CVI_U8 data_st7701_38[] = { 0x29 };
static CVI_U8 data_st7701_39[] = { 0x35, 0x00 };


const struct dsc_instr dsi_init_cmds_st7701_480x640[] = {
	{.delay = 0, .data_type = 0x39, .size = 6, .data = data_st7701_0 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_1 },
	{.delay = 0, .data_type = 0x39, .size = 6, .data = data_st7701_2 },
	{.delay = 0, .data_type = 0x39, .size = 3, .data = data_st7701_3 },
	{.delay = 0, .data_type = 0x39, .size = 3, .data = data_st7701_4 },
	{.delay = 0, .data_type = 0x39, .size = 3, .data = data_st7701_5 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_6 },
	{.delay = 0, .data_type = 0x39, .size = 17, .data = data_st7701_7 },
	{.delay = 0, .data_type = 0x39, .size = 17, .data = data_st7701_8 },
	{.delay = 0, .data_type = 0x39, .size = 6, .data = data_st7701_9 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_10 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_11 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_12 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_13 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_14 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_15 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_16 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_17 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_18 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_19 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_20 },
	{.delay = 0, .data_type = 0x39, .size = 4, .data = data_st7701_21 },
	{.delay = 0, .data_type = 0x39, .size = 12, .data = data_st7701_22 },
	{.delay = 0, .data_type = 0x39, .size = 14, .data = data_st7701_23 },
	{.delay = 0, .data_type = 0x39, .size = 5, .data = data_st7701_24 },
	{.delay = 0, .data_type = 0x39, .size = 3, .data = data_st7701_25 },
	{.delay = 0, .data_type = 0x39, .size = 17, .data = data_st7701_26 },
	{.delay = 0, .data_type = 0x39, .size = 5, .data = data_st7701_27 },
	{.delay = 0, .data_type = 0x39, .size = 3, .data = data_st7701_28 },
	{.delay = 0, .data_type = 0x39, .size = 17, .data = data_st7701_29 },
	{.delay = 0, .data_type = 0x39, .size = 4, .data = data_st7701_30 },
	{.delay = 0, .data_type = 0x39, .size = 8, .data = data_st7701_31 },
	{.delay = 0, .data_type = 0x39, .size = 4, .data = data_st7701_32 },
	{.delay = 0, .data_type = 0x39, .size = 17, .data = data_st7701_33 },
	{.delay = 0, .data_type = 0x39, .size = 7, .data = data_st7701_34 },
	{.delay = 0, .data_type = 0x39, .size = 6, .data = data_st7701_35 },
	{.delay = 120, .data_type = 0x05, .size = 1, .data = data_st7701_36 },
	{.delay = 20, .data_type = 0x15, .size = 2, .data = data_st7701_37 },
	{.delay = 0, .data_type = 0x05, .size = 1, .data = data_st7701_38 },
	{.delay = 0, .data_type = 0x15, .size = 2, .data = data_st7701_39 }
};
#endif

#else
#error "MIPI_TX_PARAM multi-delcaration!!"
#endif // _MIPI_TX_PARAM_ST_7701_H_

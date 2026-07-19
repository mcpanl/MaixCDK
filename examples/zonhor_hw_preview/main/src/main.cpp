/**
 * Zonhor HW preview — sample_sensor_lcd pipeline + status HUD.
 * Usage: zonhor_hw_preview [seconds] [1080p|5m|5m-full] [-m] [-f]
 */
#include "maix_basic.hpp"
#include "zonhor_mmf.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unistd.h>

using namespace maix;

static bool read_sysfs_int(const char *path, int &out)
{
	std::ifstream f(path);
	if (!f)
		return false;
	int v = 0;
	f >> v;
	if (!f)
		return false;
	out = v;
	return true;
}

static void read_hud_status(int &bat_valid, int &bat_pct, int &temp_valid, int &temp_c)
{
	bat_valid = 0;
	temp_valid = 0;
	bat_pct = 0;
	temp_c = 0;

	int present = 0;
	if (read_sysfs_int("/sys/class/power_supply/axp2101-battery/present", present) && present) {
		int cap = 0;
		if (read_sysfs_int("/sys/class/power_supply/axp2101-battery/capacity", cap)) {
			if (cap > 100)
				cap = 100;
			if (cap < 0)
				cap = 0;
			bat_valid = 1;
			bat_pct = cap;
		}
	}

	auto temps = sys::cpu_temp();
	auto it = temps.find("cpu");
	if (it != temps.end()) {
		temp_valid = 1;
		temp_c = (int)(it->second + 0.5f);
	}
}

int _main(int argc, char *argv[])
{
	int seconds = 8;
	const char *mode = "1080p";
	bool mirror = false;
	bool flip = false;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "-m") == 0) {
			mirror = true;
			continue;
		}
		if (strcmp(argv[i], "-f") == 0) {
			flip = true;
			continue;
		}
		if (argv[i][0] >= '0' && argv[i][0] <= '9') {
			seconds = atoi(argv[i]);
			continue;
		}
		mode = argv[i];
	}
	if (seconds <= 0)
		seconds = 8;

	const char *ini = "/mnt/system/usr/bin/sensor_cfg.ini.imx678_1080p_bin";
	CVI_U32 cam_w = 1920, cam_h = 1080;
	if (strcmp(mode, "5m") == 0 || strcmp(mode, "5MP") == 0) {
		ini = "/mnt/system/usr/bin/sensor_cfg.ini.imx678_5m";
		cam_w = 1920;
		cam_h = 1080;
	}
	if (strcmp(mode, "5m-full") == 0) {
		ini = "/mnt/system/usr/bin/sensor_cfg.ini.imx678_5m";
		cam_w = 2848;
		cam_h = 1602;
	}

	log::info("zonhor_hw_preview: mode=%s ini=%s duration=%ds mirror=%d flip=%d\n",
		  mode, ini, seconds, (int)mirror, (int)flip);

	ZONHOR_MMF_CFG_S cfg;
	ZONHOR_MMF_DefaultConfig(&cfg);
	cfg.cam_w = cam_w;
	cfg.cam_h = cam_h;
	cfg.sensor_ini = ini;
	cfg.mirror = mirror ? CVI_TRUE : CVI_FALSE;
	cfg.flip = flip ? CVI_TRUE : CVI_FALSE;

	if (ZONHOR_MMF_Init(&cfg) != CVI_SUCCESS) {
		log::error("ZONHOR_MMF_Init failed\n");
		return -1;
	}

	ZONHOR_FB_LCD_S fb;
	if (ZONHOR_FB_Open(&fb, ZONHOR_FB_LCD_DEV) != 0) {
		ZONHOR_MMF_Deinit();
		return -1;
	}
	ZONHOR_FB_Clear(&fb, 0x0000);

	const size_t scratch_n = (size_t)ZONHOR_MMF_DISP_W * ZONHOR_MMF_DISP_H; /* 192x320 buffer; blit uses valid 172x320 */
	uint16_t *scratch = (uint16_t *)calloc(scratch_n, sizeof(uint16_t));
	if (!scratch) {
		ZONHOR_FB_Close(&fb);
		ZONHOR_MMF_Deinit();
		return -1;
	}

	int ok = 0, fail = 0;
	uint64_t t0 = time::ticks_ms();
	while (!app::need_exit() && (int)(time::ticks_ms() - t0) < seconds * 1000) {
		int r = ZONHOR_FB_BlitPreview(&fb, scratch, scratch_n, 1000);
		if (r == 0) {
			int bat_v = 0, bat_pct = 0, temp_v = 0, temp_c = 0;
			read_hud_status(bat_v, bat_pct, temp_v, temp_c);
			ZONHOR_FB_DrawStatusHud(&fb, bat_v, bat_pct, temp_v, temp_c);
			++ok;
		} else {
			++fail;
		}
	}

	log::info("preview frames ok=%d fail=%d\n", ok, fail);

	free(scratch);
	ZONHOR_FB_Clear(&fb, 0x0000);
	ZONHOR_FB_Close(&fb);
	ZONHOR_MMF_Deinit();
	return (ok > 0) ? 0 : -1;
}

int main(int argc, char *argv[])
{
	sys::register_default_signal_handle();
	CATCH_EXCEPTION_RUN_RETURN(_main, -1, argc, argv);
}

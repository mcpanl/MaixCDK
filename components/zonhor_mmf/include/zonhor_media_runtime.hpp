/**
 * Process-wide refcounted wrapper around ZONHOR_MMF_Init/Deinit
 * (profile-driven graph runtime).
 * Replaces maix::cvi::MediaRuntime for PLATFORM_ZONHOR vision path.
 */
#pragma once

#include "zonhor_mmf.h"
#include <mutex>
#include <string>

namespace maix::zonhor {

class MediaRuntime {
public:
	static void acquire();
	static void release();
	static bool is_inited();

	/** Set pending sensor ini; applied on next init / reconfigure. */
	static void set_sensor_ini_path(const char *path);
	static const char *sensor_ini_path();

	/**
	 * If pending ini differs from active and refcount==0, reinit.
	 * Call after set_sensor_ini_path, before acquire when switching modes.
	 */
	static CVI_S32 reconfigure_sensor_if_needed();

	static CVI_S32 set_cam_size(CVI_U32 w, CVI_U32 h, CVI_S32 fps);
	static void get_sensor_size(CVI_U32 *w, CVI_U32 *h);

private:
	static void _init();
	static void _deinit();
	static void _at_exit_handler();

	static std::mutex s_mutex;
	static int s_refcount;
	static bool s_inited;
	static bool s_atexit_registered;
	static std::string s_sensor_ini_pending;
	static std::string s_sensor_ini_active;
	static CVI_U32 s_cam_w;
	static CVI_U32 s_cam_h;
	static CVI_S32 s_cam_fps;
};

} // namespace maix::zonhor

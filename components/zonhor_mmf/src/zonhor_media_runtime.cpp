/**
 * Refcounted ZONHOR_MMF lifecycle for Camera (and optional HW preview users).
 */

#include "zonhor_media_runtime.hpp"
#include <cstdio>
#include <cstdlib>

namespace maix::zonhor {

std::mutex MediaRuntime::s_mutex;
int MediaRuntime::s_refcount = 0;
bool MediaRuntime::s_inited = false;
bool MediaRuntime::s_atexit_registered = false;
std::string MediaRuntime::s_sensor_ini_pending;
std::string MediaRuntime::s_sensor_ini_active;
CVI_U32 MediaRuntime::s_cam_w = 1080;
CVI_U32 MediaRuntime::s_cam_h = 1920;
CVI_S32 MediaRuntime::s_cam_fps = 30;

void MediaRuntime::set_sensor_ini_path(const char *path)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (!path || path[0] == '\0') {
		s_sensor_ini_pending.clear();
		ZONHOR_MMF_SetSensorIniPath(nullptr);
		return;
	}
	s_sensor_ini_pending = path;
	ZONHOR_MMF_SetSensorIniPath(path);
}

const char *MediaRuntime::sensor_ini_path()
{
	if (!s_sensor_ini_active.empty())
		return s_sensor_ini_active.c_str();
	if (!s_sensor_ini_pending.empty())
		return s_sensor_ini_pending.c_str();
	const char *p = ZONHOR_MMF_GetSensorIniPath();
	return p ? p : "";
}

void MediaRuntime::_init()
{
	ZONHOR_MMF_CFG_S cfg;
	ZONHOR_MMF_DefaultConfig(&cfg);
	cfg.cam_w = s_cam_w;
	cfg.cam_h = s_cam_h;
	cfg.cam_fps = s_cam_fps;
	if (!s_sensor_ini_pending.empty()) {
		cfg.sensor_ini = s_sensor_ini_pending.c_str();
		ZONHOR_MMF_SetSensorIniPath(s_sensor_ini_pending.c_str());
	}

	CVI_S32 ret = ZONHOR_MMF_Init(&cfg);
	s_inited = (ret == CVI_SUCCESS) && ZONHOR_MMF_IsInited();
	if (s_inited) {
		s_sensor_ini_active = s_sensor_ini_pending;
		if (s_sensor_ini_active.empty()) {
			const char *p = ZONHOR_MMF_GetSensorIniPath();
			if (p && p[0])
				s_sensor_ini_active = p;
		}
		printf("[ZonhorMediaRuntime] init ok (ini=%s cam=%ux%u)\n",
		       s_sensor_ini_active.empty() ? "(default)" : s_sensor_ini_active.c_str(),
		       s_cam_w, s_cam_h);
	} else {
		printf("[ZonhorMediaRuntime] init failed: 0x%x\n", ret);
		s_sensor_ini_active.clear();
	}
}

void MediaRuntime::_deinit()
{
	ZONHOR_MMF_Deinit();
	s_inited = false;
	s_sensor_ini_active.clear();
	printf("[ZonhorMediaRuntime] deinit ok\n");
}

void MediaRuntime::_at_exit_handler()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (!s_inited)
		return;
	_deinit();
	s_refcount = 0;
}

CVI_S32 MediaRuntime::reconfigure_sensor_if_needed()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_sensor_ini_pending.empty())
		return CVI_SUCCESS;
	if (!s_inited) {
		ZONHOR_MMF_SetSensorIniPath(s_sensor_ini_pending.c_str());
		return CVI_SUCCESS;
	}
	if (s_sensor_ini_pending == s_sensor_ini_active)
		return CVI_SUCCESS;
	if (s_refcount > 0) {
		printf("[ZonhorMediaRuntime] cannot reconfigure while refcount=%d\n", s_refcount);
		return CVI_FAILURE;
	}
	printf("[ZonhorMediaRuntime] reconfigure %s -> %s\n",
	       s_sensor_ini_active.c_str(), s_sensor_ini_pending.c_str());
	ZONHOR_MMF_SetSensorIniPath(s_sensor_ini_pending.c_str());
	_deinit();
	_init();
	return s_inited ? CVI_SUCCESS : CVI_FAILURE;
}

void MediaRuntime::acquire()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (!s_atexit_registered) {
		std::atexit(_at_exit_handler);
		s_atexit_registered = true;
	}
	if (s_refcount == 0 && !s_inited)
		_init();
	if (s_inited)
		++s_refcount;
}

void MediaRuntime::release()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	if (s_refcount <= 0)
		return;
	--s_refcount;
	if (s_refcount == 0 && s_inited)
		_deinit();
}

bool MediaRuntime::is_inited()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_inited;
}

CVI_S32 MediaRuntime::set_cam_size(CVI_U32 w, CVI_U32 h, CVI_S32 fps)
{
	std::lock_guard<std::mutex> lock(s_mutex);
	s_cam_w = w;
	s_cam_h = h;
	if (fps != 0)
		s_cam_fps = fps;
	if (!s_inited)
		return CVI_SUCCESS;
	return ZONHOR_MMF_SetCamSize(w, h, s_cam_fps);
}

void MediaRuntime::get_sensor_size(CVI_U32 *w, CVI_U32 *h)
{
	ZONHOR_MMF_GetSensorSize(w, h);
}

} // namespace maix::zonhor

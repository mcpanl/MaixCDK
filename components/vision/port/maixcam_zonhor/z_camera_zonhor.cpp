/**
 * Camera implementation for PLATFORM_ZONHOR using zonhor_mmf.
 *
 * Pipeline (all rotation in VPSS GDC, no CPU rotate):
 *   VI → VPSS0 chn0 NV21+ROT90 → VPSS2 CSC RGB888 → Camera::read
 *   VI → VPSS0 chn1 NV21+ROT90 → VPSS1 CSC RGB888 → HW LCD preview
 *
 * User width/height follow product orientation (portrait: 1080x1920).
 */

#include "z_camera.hpp"
#include "maix_basic.hpp"
#include "zonhor_media_runtime.hpp"
#include "z_zonhor_sensor.hpp"

extern "C" {
#include "zonhor_mmf.h"
#include "sample_comm.h"
}

#include <cstring>
#include <cstdlib>
#include <algorithm>

#define MMF_SENSOR_NAME "MMF_SENSOR_NAME"
#define MAIX_SENSOR_FPS "MAIX_SENSOR_FPS"

using namespace maix;

namespace maix::camera {

static bool s_regs_flag = false;
static zonhor::Imx678Mode s_active_imx678_mode = zonhor::Imx678Mode::Binning1080p;

std::vector<std::string> list_devices()
{
	log::warn("Camera not driven by device files on Zonhor.");
	return {};
}

void set_regs_enable(bool enable) { s_regs_flag = enable; }

std::string get_device_name() { return zonhor::device_name(); }

err::Err Camera::show_colorbar(bool enable)
{
	_show_colorbar = enable;
	return err::ERR_NONE;
}

bool Camera::_check_format(image::Format fmt)
{
	switch (fmt) {
	case image::FMT_RGB888:
	case image::FMT_BGR888:
	case image::FMT_RGBA8888:
	case image::FMT_BGRA8888:
	case image::FMT_YVU420SP:
	case image::FMT_GRAYSCALE:
		return true;
	default:
		return false;
	}
}

static void _config_sensor_env(double fps)
{
	if (!getenv(MMF_SENSOR_NAME))
		setenv(MMF_SENSOR_NAME, zonhor::device_name(), 0);
	if (!getenv(MAIX_SENSOR_FPS)) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", (int)fps);
		setenv(MAIX_SENSOR_FPS, buf, 0);
	}
}

static void _prepare_sensor_mode(int width, int height)
{
	zonhor::Imx678Mode mode = zonhor::mode_from_resolution(width, height);
	const char *ini = zonhor::ini_path_for_mode(mode);
	s_active_imx678_mode = mode;
	maix::zonhor::MediaRuntime::set_sensor_ini_path(ini);
	log::info("zonhor IMX678 mode=%s (%dx%d) ini=%s",
		  (mode == zonhor::Imx678Mode::Mode5MP) ? "5MP" : "1080p_bin",
		  width, height, ini);
}

static image::Image *_read_rgb888_frame(int want_w, int want_h, int block_ms)
{
	VIDEO_FRAME_INFO_S frame;
	memset(&frame, 0, sizeof(frame));

	CVI_S32 ret = ZONHOR_MMF_CamGetFrame(&frame, (CVI_S32)block_ms);
	if (ret != CVI_SUCCESS) {
		log::error("ZONHOR_MMF_CamGetFrame failed: 0x%x", ret);
		return nullptr;
	}

	VIDEO_FRAME_S *vf = &frame.stVFrame;
	const uint32_t fw = vf->u32Width;
	const uint32_t fh = vf->u32Height;
	if (fw == 0 || fh == 0) {
		ZONHOR_MMF_CamReleaseFrame(&frame);
		return nullptr;
	}

	CVI_BOOL need_unmap = CVI_FALSE;
	uint8_t *src = vf->pu8VirAddr[0];
	if (!src) {
		src = (uint8_t *)CVI_SYS_MmapCache(vf->u64PhyAddr[0], vf->u32Length[0]);
		need_unmap = CVI_TRUE;
	}
	if (src)
		CVI_SYS_IonInvalidateCache(vf->u64PhyAddr[0], src, vf->u32Length[0]);

	int out_w = (want_w > 0) ? want_w : (int)fw;
	int out_h = (want_h > 0) ? want_h : (int)fh;

	image::Image *img = new image::Image(out_w, out_h, image::FMT_RGB888);
	if (img && src) {
		uint8_t *dst = (uint8_t *)img->data();
		const uint32_t stride = vf->u32Stride[0];
		const uint32_t row_bytes = (uint32_t)std::min(out_w * 3, (int)(fw * 3));
		for (int y = 0; y < out_h && (uint32_t)y < fh; ++y) {
			memcpy(dst + (size_t)y * out_w * 3,
			       src + (size_t)y * stride,
			       row_bytes);
		}
	}

	if (need_unmap)
		CVI_SYS_Munmap(src, vf->u32Length[0]);
	ZONHOR_MMF_CamReleaseFrame(&frame);
	return img;
}

Camera::Camera(int width, int height, image::Format format,
	       const char *device, double fps, int buff_num,
	       bool open, bool raw)
{
	err::check_bool_raise(_check_format(format), "Format not supported");

	_width = (width <= 0) ? zonhor::kDefaultUserW : width;
	_height = (height <= 0) ? zonhor::kDefaultUserH : height;
	_format = format;
	_format_impl = format;
	_buff_num = buff_num;
	_fps = (fps <= 0) ? ((_width <= 1280 && _height <= 720) ? 60 : 30) : fps;
	_ch = 0;
	_show_colorbar = false;
	_open_set_regs = s_regs_flag;
	_device = "";
	_last_read_us = time::ticks_us();
	_invert_flip = false;
	_invert_mirror = false;
	_is_opened = false;
	_hmirror = 0;
	_vflip = 0;
	_exposure = 0;
	_gain = 0;
	_param = nullptr;

	(void)device;
	(void)raw;

	if (open) {
		err::Err e = this->open(_width, _height, _format, _fps, _buff_num);
		err::check_raise(e, "camera open failed");
	}
}

Camera::~Camera()
{
	if (is_opened())
		close();
}

int Camera::get_ch_nums() { return 1; }
int Camera::get_channel() { return _ch; }

err::Err Camera::open(int width, int height, image::Format format,
		      double fps, int buff_num)
{
	if (_is_opened)
		return err::ERR_NONE;

	int w = (width <= 0) ? _width : width;
	int h = (height <= 0) ? _height : height;
	double f = (fps <= 0) ? _fps : fps;
	image::Format fmt = (format == image::FMT_INVALID) ? _format : format;
	(void)buff_num;

	err::check_bool_raise(_check_format(fmt), "Format not supported");
	zonhor::clamp_resolution(w, h);

	_width = w;
	_height = h;
	_fps = f;
	_format = fmt;
	_format_impl = fmt;

	_config_sensor_env(_fps);
	_prepare_sensor_mode(_width, _height);

	/* User coords; zonhor_mmf applies VPSS GDC ROT90 internally when portrait. */
	(void)maix::zonhor::MediaRuntime::set_cam_size((CVI_U32)_width, (CVI_U32)_height, (CVI_S32)_fps);
	(void)maix::zonhor::MediaRuntime::reconfigure_sensor_if_needed();
	maix::zonhor::MediaRuntime::acquire();
	if (!maix::zonhor::MediaRuntime::is_inited()) {
		log::error("Camera open failed: zonhor_mmf / VI init failed");
		return err::ERR_RUNTIME;
	}

	CVI_S32 ret = maix::zonhor::MediaRuntime::set_cam_size(
		(CVI_U32)_width, (CVI_U32)_height, (CVI_S32)_fps);
	if (ret != CVI_SUCCESS)
		log::warn("SetCamSize failed 0x%x — using init size", ret);

	_is_opened = true;
	return err::ERR_NONE;
}

void Camera::close()
{
	if (!_is_opened)
		return;
	_is_opened = false;
	maix::zonhor::MediaRuntime::release();
}

image::Image *Camera::read(void * /*buff*/, size_t /*buff_size*/,
			   bool block, int block_ms)
{
	if (!_is_opened) {
		err::Err e = open(_width, _height, _format, _fps, _buff_num);
		err::check_raise(e, "camera open failed");
	}

	int timeout = block ? (block_ms <= 0 ? 2000 : block_ms) : 50;
	image::Image *img = _read_rgb888_frame(_width, _height, timeout);
	if (!img)
		return nullptr;
	if (_format == image::FMT_RGB888)
		return img;

	image::Image *out = img->to_format(_format);
	delete img;
	return out;
}

image::Image *Camera::read_raw()
{
	log::error("read_raw not supported in zonhor_mmf backend");
	return nullptr;
}

void Camera::clear_buff()
{
	for (int i = 0; i < 2; ++i) {
		VIDEO_FRAME_INFO_S f;
		memset(&f, 0, sizeof(f));
		if (ZONHOR_MMF_CamGetFrame(&f, 50) == CVI_SUCCESS)
			ZONHOR_MMF_CamReleaseFrame(&f);
	}
}

void Camera::skip_frames(int num)
{
	for (int i = 0; i < num; ++i) {
		image::Image *img = this->read();
		delete img;
	}
}

bool Camera::is_opened() { return _is_opened; }

Camera *Camera::add_channel(int width, int height, image::Format format,
			    double fps, int buff_num, bool open)
{
	(void)width;
	(void)height;
	(void)format;
	(void)fps;
	(void)buff_num;
	(void)open;
	log::error("add_channel not supported on zonhor_mmf (VPSS0 chn1 used for LCD ROT)");
	return nullptr;
}

err::Err Camera::set_resolution(int width, int height)
{
	if (!_is_opened)
		return err::ERR_NOT_OPEN;

	zonhor::clamp_resolution(width, height);
	zonhor::Imx678Mode new_mode = zonhor::mode_from_resolution(width, height);
	if (new_mode != s_active_imx678_mode) {
		log::info("zonhor set_resolution cross-mode %dx%d → rebuild MMF", width, height);
		close();
		return open(width, height, _format, _fps, _buff_num);
	}

	CVI_S32 ret = maix::zonhor::MediaRuntime::set_cam_size(
		(CVI_U32)width, (CVI_U32)height, (CVI_S32)_fps);
	if (ret != CVI_SUCCESS)
		return err::ERR_RUNTIME;
	_width = width;
	_height = height;
	return err::ERR_NONE;
}

err::Err Camera::set_fps(double fps)
{
	log::warn("set_fps not fully supported in zonhor_mmf backend");
	_fps = fps;
	return err::ERR_NONE;
}

int Camera::hmirror(int value)
{
	if (value != -1) {
		_hmirror = value;
		(void)ZONHOR_MMF_SetMirrorFlip(_hmirror ? CVI_TRUE : CVI_FALSE,
						_vflip ? CVI_TRUE : CVI_FALSE);
	}
	return _hmirror;
}

int Camera::vflip(int value)
{
	if (value != -1) {
		_vflip = value;
		(void)ZONHOR_MMF_SetMirrorFlip(_hmirror ? CVI_TRUE : CVI_FALSE,
						_vflip ? CVI_TRUE : CVI_FALSE);
	}
	return _vflip;
}

int Camera::exposure(int value)
{
	log::warn("exposure not supported");
	return value;
}

int Camera::gain(int value)
{
	log::warn("gain not supported");
	return value;
}

int Camera::luma(int value)
{
	log::warn("luma not supported");
	return value;
}

int Camera::constrast(int value)
{
	log::warn("constrast not supported");
	return value;
}

int Camera::saturation(int value)
{
	log::warn("saturation not supported");
	return value;
}

int Camera::awb_mode(int value)
{
	log::warn("awb_mode not supported");
	return value;
}

int Camera::set_awb(int value)
{
	log::warn("set_awb not supported");
	return value;
}

int Camera::exp_mode(int value)
{
	log::warn("exp_mode not supported");
	return value;
}

err::Err Camera::set_windowing(std::vector<int> roi)
{
	(void)roi;
	log::warn("set_windowing not supported");
	return err::ERR_NONE;
}

std::vector<int> Camera::get_sensor_size()
{
	CVI_U32 w = 0, h = 0;
	maix::zonhor::MediaRuntime::get_sensor_size(&w, &h);
	if (w > 0 && h > 0)
		return zonhor::sensor_size_user_oriented((int)w, (int)h);
	return zonhor::sensor_size_for_mode(s_active_imx678_mode);
}

err::Err Camera::write_reg(int addr, int data, int bit_width)
{
	(void)addr;
	(void)data;
	(void)bit_width;
	return err::ERR_NONE;
}

int Camera::read_reg(int addr, int bit_width)
{
	(void)addr;
	(void)bit_width;
	return -1;
}

} // namespace maix::camera

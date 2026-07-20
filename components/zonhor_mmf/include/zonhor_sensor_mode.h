/**
 * IMX678 sensor mode policy for Zonhor.
 *
 * Board modes (via sensor_cfg.ini):
 *   - imx678_1080p_bin  → 2x2 binning, full FOV, ISP 1920x1080
 *   - imx678_5m         → 4K center-crop to ~5MP (up to 2848x1602)
 *
 * Product rule: user Camera size is orientation-agnostic for mode pick.
 * Camera(1920,1080) and Camera(1080,1920) both mean the 1080p envelope and
 * MUST select 2x2 binning — never the 4K→5MP crop path.
 *
 * Priority when opening the graph:
 *   1. env MAIX_SENSOR_CFG_INI
 *   2. explicit sensor_ini / SetSensorIniPath
 *   3. size-based default (this helper)
 */
#ifndef __ZONHOR_SENSOR_MODE_H__
#define __ZONHOR_SENSOR_MODE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ZONHOR_SNS_MODE_1080P_BIN = 0, /* 2x2 binning 1920x1080 */
	ZONHOR_SNS_MODE_5MP       = 1, /* 4K crop ~5MP */
} zonhor_sns_mode_e;

#define ZONHOR_SNS_BIN_MAX_W  1920u
#define ZONHOR_SNS_BIN_MAX_H  1080u
#define ZONHOR_SNS_5MP_MAX_W  2848u
#define ZONHOR_SNS_5MP_MAX_H  1602u

/* Default Camera() size in user (portrait) coordinates. */
#define ZONHOR_SNS_DEFAULT_USER_W  ZONHOR_SNS_BIN_MAX_H /* 1080 */
#define ZONHOR_SNS_DEFAULT_USER_H  ZONHOR_SNS_BIN_MAX_W /* 1920 */

#define ZONHOR_SNS_INI_1080P_BIN \
	"/mnt/system/usr/bin/sensor_cfg.ini.imx678_1080p_bin"
#define ZONHOR_SNS_INI_5MP \
	"/mnt/system/usr/bin/sensor_cfg.ini.imx678_5m"

/**
 * Pick mode from requested WxH using long/short side envelope.
 * Orientation does not matter: 1920x1080 == 1080x1920 → 1080P_BIN.
 */
zonhor_sns_mode_e zonhor_sns_mode_from_size(uint32_t w, uint32_t h);

const char *zonhor_sns_ini_for_mode(zonhor_sns_mode_e mode);
const char *zonhor_sns_ini_for_size(uint32_t w, uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* __ZONHOR_SENSOR_MODE_H__ */

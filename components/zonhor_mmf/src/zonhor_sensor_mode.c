/**
 * IMX678 mode selection — prefer 2x2 binning for the 1080p envelope.
 */

#include "zonhor_sensor_mode.h"

zonhor_sns_mode_e zonhor_sns_mode_from_size(uint32_t w, uint32_t h)
{
	uint32_t long_side  = (w > h) ? w : h;
	uint32_t short_side = (w < h) ? w : h;

	/*
	 * Envelope compare (not raw WxH): portrait 1080x1920 stays binning.
	 * Only requests that need more than 1920x1080 ISP readout use 5MP crop.
	 */
	if (long_side <= ZONHOR_SNS_BIN_MAX_W && short_side <= ZONHOR_SNS_BIN_MAX_H)
		return ZONHOR_SNS_MODE_1080P_BIN;
	return ZONHOR_SNS_MODE_5MP;
}

const char *zonhor_sns_ini_for_mode(zonhor_sns_mode_e mode)
{
	return (mode == ZONHOR_SNS_MODE_5MP) ? ZONHOR_SNS_INI_5MP
					     : ZONHOR_SNS_INI_1080P_BIN;
}

const char *zonhor_sns_ini_for_size(uint32_t w, uint32_t h)
{
	return zonhor_sns_ini_for_mode(zonhor_sns_mode_from_size(w, h));
}

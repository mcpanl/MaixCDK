/**
 * Zonhor (IMX678) sensor mode helpers for Camera.
 *
 * Policy lives in zonhor_mmf (zonhor_sensor_mode.h):
 *   - ≤1080p envelope (1920x1080 or 1080x1920) → 2x2 binning
 *   - larger → 4K crop 5MP
 *
 * User / API coordinates follow product orientation (portrait panel).
 * Example: full 1080p preview is Camera(1080, 1920).
 * ISP still outputs landscape 1920x1080 — graph Group0 GDC rotates to user size.
 *
 * Override with env MAIX_SENSOR_CFG_INI for a fixed path.
 */
#pragma once

#include "zonhor_sensor_mode.h"

#include <algorithm>
#include <vector>

namespace maix::camera::zonhor {

enum class Imx678Mode {
    Binning1080p = 0,
    Mode5MP      = 1,
};

static constexpr int kBinningMaxW = (int)ZONHOR_SNS_BIN_MAX_W;
static constexpr int kBinningMaxH = (int)ZONHOR_SNS_BIN_MAX_H;
static constexpr int k5mpMaxW     = (int)ZONHOR_SNS_5MP_MAX_W;
static constexpr int k5mpMaxH     = (int)ZONHOR_SNS_5MP_MAX_H;

/** Default Camera() resolution in user (portrait) coordinates. */
static constexpr int kDefaultUserW = (int)ZONHOR_SNS_DEFAULT_USER_W;
static constexpr int kDefaultUserH = (int)ZONHOR_SNS_DEFAULT_USER_H;

static constexpr const char *kIni1080p = ZONHOR_SNS_INI_1080P_BIN;
static constexpr const char *kIni5mp   = ZONHOR_SNS_INI_5MP;

inline Imx678Mode mode_from_resolution(int width, int height)
{
    zonhor_sns_mode_e m = zonhor_sns_mode_from_size(
        (uint32_t)std::max(1, width), (uint32_t)std::max(1, height));
    return (m == ZONHOR_SNS_MODE_5MP) ? Imx678Mode::Mode5MP
                                      : Imx678Mode::Binning1080p;
}

inline const char *ini_path_for_mode(Imx678Mode mode)
{
    return zonhor_sns_ini_for_mode(
        (mode == Imx678Mode::Mode5MP) ? ZONHOR_SNS_MODE_5MP
                                      : ZONHOR_SNS_MODE_1080P_BIN);
}

inline const char *ini_path_for_resolution(int width, int height)
{
    return ini_path_for_mode(mode_from_resolution(width, height));
}

inline void clamp_resolution(int &width, int &height)
{
    const bool portrait = width < height;
    int long_side  = std::max(width, height);
    int short_side = std::min(width, height);
    Imx678Mode mode = mode_from_resolution(width, height);
    if (mode == Imx678Mode::Binning1080p) {
        long_side  = std::max(2, std::min(long_side,  kBinningMaxW));
        short_side = std::max(2, std::min(short_side, kBinningMaxH));
    } else {
        long_side  = std::max(2, std::min(long_side,  k5mpMaxW));
        short_side = std::max(2, std::min(short_side, k5mpMaxH));
    }
    if (portrait) {
        width  = short_side;
        height = long_side;
    } else {
        width  = long_side;
        height = short_side;
    }
}

inline const char *device_name() { return "imx678"; }

/** Max sensor size in user (portrait-first) coordinates. */
inline std::vector<int> sensor_size_for_mode(Imx678Mode mode)
{
    if (mode == Imx678Mode::Mode5MP)
        return {k5mpMaxH, k5mpMaxW};
    return {kDefaultUserW, kDefaultUserH};
}

/** Convert ISP-reported WxH into user coordinates. */
inline std::vector<int> sensor_size_user_oriented(int hw_w, int hw_h)
{
    return {std::min(hw_w, hw_h), std::max(hw_w, hw_h)};
}

} // namespace maix::camera::zonhor

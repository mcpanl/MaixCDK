/**
 * Zonhor (IMX678) sensor mode helpers for Camera (x_mmf backend on Zonhor).
 *
 * Same product rule as zonhor_mmf / maixcam_zonhor:
 *   Camera(1920,1080) and Camera(1080,1920) both → 2x2 binning 1080p.
 *   Larger requests → 4K crop 5MP.
 *
 * Uses long/short side envelope (not raw WxH) so portrait init cannot
 * mis-select 5MP. Prefer MAIXCAM_VISION_BACKEND=zonhor_mmf on Zonhor.
 */
#pragma once

#include <algorithm>
#include <vector>

namespace maix::camera::zonhor {

enum class Imx678Mode {
    Binning1080p = 0,
    Mode5MP      = 1,
};

static constexpr int kBinningMaxW = 1920;
static constexpr int kBinningMaxH = 1080;
static constexpr int k5mpMaxW     = 2848;
static constexpr int k5mpMaxH     = 1602;

/** Default Camera() resolution in user (portrait) coordinates. */
static constexpr int kDefaultUserW = kBinningMaxH; /* 1080 */
static constexpr int kDefaultUserH = kBinningMaxW; /* 1920 */

static constexpr const char *kIni1080p =
    "/mnt/system/usr/bin/sensor_cfg.ini.imx678_1080p_bin";
static constexpr const char *kIni5mp =
    "/mnt/system/usr/bin/sensor_cfg.ini.imx678_5m";

inline Imx678Mode mode_from_resolution(int width, int height)
{
    const int long_side  = std::max(width, height);
    const int short_side = std::min(width, height);
    /* Orientation-agnostic: 1080x1920 stays 1080p_bin, not 5MP. */
    if (long_side <= kBinningMaxW && short_side <= kBinningMaxH)
        return Imx678Mode::Binning1080p;
    return Imx678Mode::Mode5MP;
}

inline const char *ini_path_for_mode(Imx678Mode mode)
{
    return (mode == Imx678Mode::Mode5MP) ? kIni5mp : kIni1080p;
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

inline std::vector<int> sensor_size_for_mode(Imx678Mode mode)
{
    if (mode == Imx678Mode::Mode5MP)
        return {k5mpMaxH, k5mpMaxW};
    return {kDefaultUserW, kDefaultUserH};
}

inline std::vector<int> sensor_size_user_oriented(int hw_w, int hw_h)
{
    return {std::min(hw_w, hw_h), std::max(hw_w, hw_h)};
}

} // namespace maix::camera::zonhor

/**
 * Zonhor (IMX678) sensor mode selection helpers.
 *
 * Board provides two sensor_cfg.ini variants under /mnt/system/usr/bin/:
 *   - sensor_cfg.ini.imx678_1080p_bin  → 2x2 hardware binning 1920x1080
 *   - sensor_cfg.ini.imx678_5m         → 5MP (up to 2848x1602)
 *
 * Selection is driven by Camera open()/set_resolution() request size.
 * Override with env MAIX_SENSOR_CFG_INI for a fixed path.
 */
#pragma once

#include <algorithm>
#include <cstring>
#include <string>
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

static constexpr const char *kIni1080p =
    "/mnt/system/usr/bin/sensor_cfg.ini.imx678_1080p_bin";
static constexpr const char *kIni5mp =
    "/mnt/system/usr/bin/sensor_cfg.ini.imx678_5m";

inline Imx678Mode mode_from_resolution(int width, int height)
{
    /* Treat anything within 1080p envelope as binning mode; larger → 5MP. */
    if (width <= kBinningMaxW && height <= kBinningMaxH)
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
    Imx678Mode mode = mode_from_resolution(width, height);
    if (mode == Imx678Mode::Binning1080p) {
        width  = std::max(2, std::min(width,  kBinningMaxW));
        height = std::max(2, std::min(height, kBinningMaxH));
    } else {
        width  = std::max(2, std::min(width,  k5mpMaxW));
        height = std::max(2, std::min(height, k5mpMaxH));
    }
}

inline const char *device_name() { return "imx678"; }

inline std::vector<int> sensor_size_for_mode(Imx678Mode mode)
{
    if (mode == Imx678Mode::Mode5MP)
        return {k5mpMaxW, k5mpMaxH};
    return {kBinningMaxW, kBinningMaxH};
}

} // namespace maix::camera::zonhor

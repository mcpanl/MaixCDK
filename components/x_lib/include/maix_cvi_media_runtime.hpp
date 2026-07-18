#pragma once
#ifdef __cplusplus

#include "x_mmf.h"
#include <mutex>
#include <string>

namespace maix::cvi {

/**
 * Thread-safe, reference-counted singleton that owns the X_MMF context.
 *
 * Call acquire() whenever a Display, Camera, or NN object is created.
 * Call release() when it is destroyed.  X_MMF_Init / Deinit run only
 * on the first / last reference so any creation order is safe.
 *
 * The pipeline (matching z_lib topology):
 *   VPSS group 0 — display path: user RGB888 → VPSS → NV21 → VO
 *   VPSS group 1 — camera path:  VI → VPSS → RGB888 → user
 *
 * After init the singleton also:
 *   - binds VI to VPSS group 1
 *   - binds VPSS group 0 chn 0 to VO (skipped on PLATFORM_ZONHOR — FB display)
 *   - enables / shows the VO channel with 90° rotation (MaixCam only)
 */
class MediaRuntime {
public:
    static void acquire();
    static void release();
    static X_MMF_CTX_S *ctx();
    static bool is_inited();

    /**
     * Set sensor_cfg.ini path applied on the next (or current) X_MMF init.
     * Also forwards to X_MMF_SetSensorIniPath. Call before acquire() when
     * the desired VI mode depends on resolution (e.g. Zonhor IMX678).
     *
     * If the runtime is already initialized with a different path, call
     * reconfigure_sensor() after ensuring other users have released (refcount 0),
     * or close()/open() the camera so release() tears down first.
     */
    static void set_sensor_ini_path(const char *path);
    static const char *sensor_ini_path();

    /**
     * If pending sensor ini differs from the one used at last init, tear down
     * and re-init. Requires s_refcount == 0 (no live users) OR the caller has
     * already released and will re-acquire. Returns CVI_SUCCESS on success /
     * no-op, CVI_FAILURE if other users still hold the runtime.
     */
    static CVI_S32 reconfigure_sensor_if_needed();

    /**
     * Resize / reformat VPSS display path group 0 **source** attributes (u32MaxW/H,
     * enPixelFormat) so CVI_VPSS_SendFrame matches user frames. VO binding and VPSS
     * channel output (NV21 at panel resolution) stay as initialized by X_MMF_DefaultConfig.
     * Call after acquire() when the Display object knows its width / height / format.
     * Thread-safe (uses the same mutex as acquire/release).
     */
    static CVI_S32 configure_display_vpss_input(CVI_U32 max_w, CVI_U32 max_h,
                                                PIXEL_FORMAT_E en_pixel_format);

    /**
     * Mutex serializing CVI_VPSS_SendFrame to display VPSS group 0 (XMMF display path).
     * Used by DisplayCviXmmf and by MediaRuntime init/deinit black-frame flushes so teardown
     * cannot race with a concurrent show().
     *
     * Lock order: never hold this while calling configure_display_vpss_input (take s_mutex
     * first there). Typical display path: configure_display_vpss_input (s_mutex only), then
     * lock this mutex only around CVI_VPSS_SendFrame.
     */
    static std::mutex &display_vpss0_send_mutex();

private:
    static std::mutex  s_mutex;
    static std::mutex  s_disp_vpss0_send_mutex;
    static int         s_refcount;
    static X_MMF_CTX_S s_ctx;
    static std::string s_sensor_ini_pending;
    static std::string s_sensor_ini_active;

    static void _init();
    static void _deinit();
    /** Registered with maix::util::register_exit_function; runs once at process exit if MMF was inited. */
    static void _at_exit_handler();
};

} // namespace maix::cvi
#endif // __cplusplus
